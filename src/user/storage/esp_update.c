#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/uuid.h"
#include "storage.h"

#define RESERVED 32U
#define FAT_COPIES 2U
#define EOC 0x0fffffffU
#define FAT_MASK 0x0fffffffU

struct fat_state {
	const struct storage_io *io;
	struct fat32_layout layout;
	uint8_t *fat;
	size_t fat_bytes;
};

struct fat_file {
	int found;
	uint64_t dir_offset;
	uint32_t first_cluster;
	uint32_t size;
};

struct file_change {
	const char *short_name;
	const struct esp_file *source;
	struct fat_file current;
	uint32_t *clusters;
	size_t cluster_count;
	uint32_t new_first;
};

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int read_at(const struct storage_io *io, uint64_t offset, void *data,
	size_t size)
{
	return io->read(io->context, offset, data, size);
}

static int write_verify(const struct storage_io *io, uint64_t offset,
	const void *data, size_t size)
{
	uint8_t buffer[4096];
	if (io->write(io->context, offset, data, size))
		return -EIO;
	while (size) {
		size_t amount = size > sizeof(buffer) ? sizeof(buffer) : size;
		if (io->read(io->context, offset, buffer, amount) ||
		    memcmp(buffer, data, amount))
			return -EIO;
		offset += amount;
		data = (const uint8_t *)data + amount;
		size -= amount;
	}
	return 0;
}

static uint64_t cluster_offset(const struct fat_state *fs, uint32_t cluster)
{
	return ((uint64_t)fs->layout.data_start + cluster - 2) * fs->io->sector;
}

static uint32_t fat_value(const struct fat_state *fs, uint32_t cluster)
{
	return le32(fs->fat + (size_t)cluster * 4) & FAT_MASK;
}

static void fat_set(struct fat_state *fs, uint32_t cluster, uint32_t value)
{
	storage_le32(fs->fat + (size_t)cluster * 4, value & FAT_MASK);
}

static int fat_open(struct fat_state *fs, const struct storage_io *io)
{
	uint8_t boot[4096];
	int error = fat32_plan(io, &fs->layout);
	if (error)
		return error;
	if (!io->write || !io->flush || read_at(io, 0, boot, io->sector))
		return -EIO;
	if (le16(boot + 11) != io->sector || boot[13] != 1 ||
	    le16(boot + 14) != RESERVED || boot[16] != FAT_COPIES ||
	    le32(boot + 32) != fs->layout.sectors ||
	    le32(boot + 36) != fs->layout.fat_sectors ||
	    le32(boot + 44) != 2 || boot[510] != 0x55 || boot[511] != 0xaa)
		return -EINVAL;
	fs->io = io;
	fs->fat_bytes = (size_t)fs->layout.fat_sectors * io->sector;
	fs->fat = malloc(fs->fat_bytes);
	if (!fs->fat)
		return -ENOMEM;
	if (read_at(io, (uint64_t)RESERVED * io->sector, fs->fat,
		    fs->fat_bytes)) {
		free(fs->fat);
		fs->fat = NULL;
		return -EIO;
	}
	return 0;
}

static int fat_flush(struct fat_state *fs)
{
	uint64_t second = (uint64_t)(RESERVED + fs->layout.fat_sectors) *
	    fs->io->sector;
	int error = write_verify(fs->io, second, fs->fat, fs->fat_bytes);
	if (!error)
		error = fs->io->flush(fs->io->context);
	if (!error)
		error = write_verify(fs->io, (uint64_t)RESERVED * fs->io->sector,
		    fs->fat, fs->fat_bytes);
	if (!error)
		error = fs->io->flush(fs->io->context);
	return error;
}

static int find_short(const struct fat_state *fs, uint32_t directory,
	const char name[11], struct fat_file *file, uint64_t *free_offset)
{
	uint8_t sector[4096];
	uint32_t cluster = directory;
	uint64_t first_free = UINT64_MAX;
	memset(file, 0, sizeof(*file));
	for (uint32_t hops = 0; hops < fs->layout.clusters; hops++) {
		if (cluster < 2 || cluster >= fs->layout.clusters + 2)
			return -EINVAL;
		if (read_at(fs->io, cluster_offset(fs, cluster), sector,
			    fs->io->sector))
			return -EIO;
		for (uint32_t offset = 0; offset < fs->io->sector; offset += 32) {
			uint8_t *entry = sector + offset;
			uint64_t absolute = cluster_offset(fs, cluster) + offset;
			if (entry[0] == 0) {
				if (first_free == UINT64_MAX)
					first_free = absolute;
				goto done;
			}
			if (entry[0] == 0xe5) {
				if (first_free == UINT64_MAX)
					first_free = absolute;
				continue;
			}
			if ((entry[11] & 0x08) || entry[11] == 0x0f)
				continue;
			if (memcmp(entry, name, 11) == 0) {
				file->found = 1;
				file->dir_offset = absolute;
				file->first_cluster = (uint32_t)le16(entry + 20) << 16 |
				    le16(entry + 26);
				file->size = le32(entry + 28);
				goto done;
			}
		}
		cluster = fat_value(fs, cluster);
		if (cluster >= 0x0ffffff8)
			break;
	}
done:
	if (free_offset)
		*free_offset = first_free;
	return 0;
}

static int file_bytes(const struct fat_state *fs, const struct fat_file *file,
	uint8_t **out)
{
	if (!file->found || !file->size || !file->first_cluster)
		return -EINVAL;
	size_t allocated = ((size_t)file->size + fs->io->sector - 1) /
	    fs->io->sector * fs->io->sector;
	uint8_t *bytes = malloc(allocated);
	if (!bytes)
		return -ENOMEM;
	uint32_t cluster = file->first_cluster;
	size_t remaining = file->size, offset = 0;
	for (uint32_t hops = 0; remaining && hops < fs->layout.clusters; hops++) {
		size_t amount = remaining > fs->io->sector ? fs->io->sector : remaining;
		if (cluster < 2 || cluster >= fs->layout.clusters + 2 ||
		    read_at(fs->io, cluster_offset(fs, cluster), bytes + offset,
			    fs->io->sector)) {
			free(bytes);
			return -EIO;
		}
		remaining -= amount;
		offset += amount;
		if (remaining)
			cluster = fat_value(fs, cluster);
	}
	if (remaining) {
		free(bytes);
		return -EINVAL;
	}
	*out = bytes;
	return 0;
}

static int old_chain(struct fat_state *fs, const struct fat_file *file)
{
	uint32_t cluster = file->first_cluster;
	for (uint32_t hops = 0; cluster >= 2 && cluster < fs->layout.clusters + 2 &&
	    hops < fs->layout.clusters; hops++) {
		uint32_t next = fat_value(fs, cluster);
		fat_set(fs, cluster, 0);
		if (next >= 0x0ffffff8)
			return 0;
		cluster = next;
	}
	return -EINVAL;
}

static int write_new_file(struct fat_state *fs, struct file_change *change)
{
	size_t bytes = (size_t)change->source->bytes;
	size_t count = (bytes + fs->io->sector - 1) / fs->io->sector;
	if (!bytes || bytes > UINT32_MAX || !count)
		return -EINVAL;
	change->clusters = malloc(count * sizeof(*change->clusters));
	if (!change->clusters)
		return -ENOMEM;
	change->cluster_count = count;
	size_t found = 0;
	for (uint32_t cluster = 6; cluster < fs->layout.clusters + 2 && found < count;
	    cluster++)
		if (fat_value(fs, cluster) == 0)
			change->clusters[found++] = cluster;
	if (found != count)
		return -ENOSPC;
	change->new_first = change->clusters[0];
	uint8_t sector[4096];
	uint64_t offset = 0;
	for (size_t i = 0; i < count; i++) {
		size_t amount = bytes - (size_t)offset;
		if (amount > fs->io->sector)
			amount = fs->io->sector;
		memset(sector, 0, fs->io->sector);
		int error = change->source->read(change->source->context, offset,
		    sector, amount);
		if (error)
			return error;
		uint64_t target = cluster_offset(fs, change->clusters[i]);
		if (write_verify(fs->io, target, sector, fs->io->sector))
			return -EIO;
		offset += amount;
	}
	for (size_t i = 0; i < count; i++)
		fat_set(fs, change->clusters[i],
		    i + 1 == count ? EOC : change->clusters[i + 1]);
	return 0;
}

static int directory_write_entry(const struct fat_state *fs,
	const struct file_change *change, const char short_name[11])
{
	uint8_t sector[4096];
	uint64_t offset = change->current.dir_offset;
	uint64_t sector_offset = offset - offset % fs->io->sector;
	if (read_at(fs->io, sector_offset, sector, fs->io->sector))
		return -EIO;
	uint8_t *entry = sector + (offset - sector_offset);
	if (!change->current.found) {
		memset(entry, 0, 32);
		memcpy(entry, short_name, 11);
		entry[11] = 0x20;
	}
	storage_le16(entry + 20, (uint16_t)(change->new_first >> 16));
	storage_le16(entry + 26, (uint16_t)change->new_first);
	storage_le32(entry + 28, (uint32_t)change->source->bytes);
	int error = write_verify(fs->io, sector_offset, sector, fs->io->sector);
	return error ? error : fs->io->flush(fs->io->context);
}

static int config_write(struct fat_state *fs, const struct fat_file *file,
	const char *config, size_t length)
{
	if (!file->found || !file->first_cluster ||
	    length > fs->io->sector)
		return -ENOSPC;
	uint8_t sector[4096];
	memset(sector, 0, fs->io->sector);
	memcpy(sector, config, length);
	if (write_verify(fs->io, cluster_offset(fs, file->first_cluster),
	    sector, fs->io->sector))
		return -EIO;
	uint64_t sector_offset = file->dir_offset - file->dir_offset % fs->io->sector;
	if (read_at(fs->io, sector_offset, sector, fs->io->sector))
		return -EIO;
	storage_le32(sector + file->dir_offset - sector_offset + 28,
	    (uint32_t)length);
	int error = write_verify(fs->io, sector_offset, sector, fs->io->sector);
	return error ? error : fs->io->flush(fs->io->context);
}

struct memory_file { void *data; size_t bytes; };

static int memory_read(void *context, uint64_t offset, void *buffer, size_t size)
{
	struct memory_file *file = context;
	if (offset > file->bytes || size > file->bytes - offset)
		return -EINVAL;
	memcpy(buffer, (uint8_t *)file->data + offset, size);
	return 0;
}

static void memory_file_init(struct esp_file *file, struct memory_file *memory,
	void *data, size_t bytes)
{
	memory->data = data;
	memory->bytes = bytes;
	*file = (struct esp_file){.context = memory, .bytes = bytes,
	    .read = memory_read};
}

int esp_update_slots(const struct storage_io *io, const struct esp_file *kernel,
	const struct esp_file *initramfs, char active_slot, const uint8_t root_uuid[16],
	char *new_slot)
{
	static const char kernel_a[] = "KERNELA ELF";
	static const char init_a[] = "INITA   CPI";
	static const char kernel_b[] = "KERNELB ELF";
	static const char init_b[] = "INITB   CPI";
	static const char legacy_kernel[] = "KERNEL  ELF";
	static const char legacy_init[] = "INIT    CPI";
	static const char config_name[] = "LIMINE~1CNF";
	struct fat_state fs = {0};
	struct fat_file ka, ia, kb, ib, lk, li, config;
	uint8_t *old_kernel = NULL, *old_init = NULL;
	char config_text[512], root[37];
	struct esp_file old_kernel_file = {0}, old_init_file = {0};
	struct memory_file old_kernel_memory = {0}, old_init_memory = {0};
	struct file_change changes[4] = {0};
	const char *inactive, *active;
	int error, migrating;
	if (!io || !kernel || !initramfs || !root_uuid || !new_slot ||
	    (active_slot != 'A' && active_slot != 'B'))
		return -EINVAL;
	error = fat_open(&fs, io);
	if (error)
		goto done;
	/* The project formatter creates fixed, one-cluster /boot and EFI/BOOT dirs. */
	if ((error = find_short(&fs, 5, kernel_a, &ka, NULL)) ||
	    (error = find_short(&fs, 5, init_a, &ia, NULL)) ||
	    (error = find_short(&fs, 5, kernel_b, &kb, NULL)) ||
	    (error = find_short(&fs, 5, init_b, &ib, NULL)) ||
	    (error = find_short(&fs, 5, legacy_kernel, &lk, NULL)) ||
	    (error = find_short(&fs, 5, legacy_init, &li, NULL)) ||
	    (error = find_short(&fs, 4, config_name, &config, NULL)))
		goto done;
	migrating = !(ka.found && ia.found && kb.found && ib.found);
	if (migrating) {
		if (!lk.found || !li.found) {
			error = -EINVAL;
			goto done;
		}
		active_slot = 'A';
		if ((error = file_bytes(&fs, &lk, &old_kernel)) ||
		    (error = file_bytes(&fs, &li, &old_init)))
			goto done;
		memory_file_init(&old_kernel_file, &old_kernel_memory, old_kernel, lk.size);
		memory_file_init(&old_init_file, &old_init_memory, old_init, li.size);
		active = "A";
		inactive = "B";
	} else {
		active = active_slot == 'A' ? "A" : "B";
		inactive = active_slot == 'A' ? "B" : "A";
	}
	/* Free only the inactive slot first; the currently booted slot stays intact. */
	struct fat_file *target_kernel = inactive[0] == 'A' ? &ka : &kb;
	struct fat_file *target_init = inactive[0] == 'A' ? &ia : &ib;
	if (!migrating) {
		if (target_kernel->found && (error = old_chain(&fs, target_kernel)))
			goto done;
		if (target_init->found && (error = old_chain(&fs, target_init)))
			goto done;
	}
	uint64_t free_kernel = UINT64_MAX, free_init = UINT64_MAX;
	if (!target_kernel->found &&
	    (error = find_short(&fs, 5,
		inactive[0] == 'A' ? kernel_a : kernel_b,
		&ka, &free_kernel)))
		goto done;
	if (!target_init->found &&
	    (error = find_short(&fs, 5,
		inactive[0] == 'A' ? init_a : init_b,
		&ia, &free_init)))
		goto done;
	struct fat_file *dir_k = inactive[0] == 'A' ? &ka : &kb;
	struct fat_file *dir_i = inactive[0] == 'A' ? &ia : &ib;
	if (!dir_k->found) {
		if (free_kernel == UINT64_MAX || free_init == UINT64_MAX) {
			error = -ENOSPC;
			goto done;
		}
		dir_k->dir_offset = free_kernel;
		dir_i->dir_offset = free_kernel + 32;
	}
	changes[0] = (struct file_change){.short_name = inactive[0] == 'A' ? kernel_a : kernel_b,
	    .source = kernel, .current = *dir_k};
	changes[1] = (struct file_change){.short_name = inactive[0] == 'A' ? init_a : init_b,
	    .source = initramfs, .current = *dir_i};
	if (migrating) {
		struct fat_file *active_k = &ka, *active_i = &ia;
		if (free_kernel == UINT64_MAX ||
		    free_kernel + 4 * 32 > cluster_offset(&fs, 5) + io->sector) {
			error = -ENOSPC;
			goto done;
		}
		uint8_t directory[4096];
		if (read_at(io, cluster_offset(&fs, 5), directory, io->sector)) {
			error = -EIO;
			goto done;
		}
		for (unsigned i = 0; i < 4; i++) {
			uint64_t offset = free_kernel + i * 32;
			uint8_t first = directory[offset - cluster_offset(&fs, 5)];
			if (first != 0 && first != 0xe5) {
				error = -ENOSPC;
				goto done;
			}
		}
		active_k->dir_offset = free_kernel + 64;
		active_i->dir_offset = free_kernel + 96;
		changes[2] = (struct file_change){.short_name = kernel_a,
		    .source = &old_kernel_file, .current = *active_k};
		changes[3] = (struct file_change){.short_name = init_a,
		    .source = &old_init_file, .current = *active_i};
	}
	for (unsigned i = 0; i < (migrating ? 4U : 2U); i++)
		if ((error = write_new_file(&fs, &changes[i])))
			goto done;
	if ((error = fat_flush(&fs)))
		goto done;
	for (unsigned i = 0; i < (migrating ? 4U : 2U); i++) {
		if ((error = directory_write_entry(&fs, &changes[i],
			    changes[i].short_name)))
			goto done;
	}
	uuid_format(root_uuid, root);
	int length = snprintf(config_text, sizeof(config_text),
	    "timeout: 10\ndefault_entry: %u\n"
	    "/OpenSillicon A\n protocol: limine\n path: boot():/boot/kernelA.elf\n"
	    " module_path: boot():/boot/initA.cpi\n cmdline: root=PARTUUID=%s kernel-slot=A\n"
	    "/OpenSillicon B\n protocol: limine\n path: boot():/boot/kernelB.elf\n"
	    " module_path: boot():/boot/initB.cpi\n cmdline: root=PARTUUID=%s kernel-slot=B\n",
	    inactive[0] == 'A' ? 1U : 2U, root, root);
	if (length < 0 || (size_t)length >= sizeof(config_text)) {
		error = -EOVERFLOW;
		goto done;
	}
	if ((error = config_write(&fs, &config, config_text, (size_t)length)))
		goto done;
	if ((error = io->flush(io->context)))
		goto done;
	*new_slot = inactive[0];
	(void)active;
done:
	for (unsigned i = 0; i < 4; i++)
		free(changes[i].clusters);
	free(old_kernel);
	free(old_init);
	free(fs.fat);
	return error;
}
