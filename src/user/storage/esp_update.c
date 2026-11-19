#include <errno.h>
#include <stdlib.h>
#include <string.h>
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
	uint64_t dir_offset;
	uint32_t first_cluster;
};

struct file_change {
	const struct esp_file *source;
	struct fat_file current;
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

/* The project formatter places each of these directories in one cluster. */
static int find_short(const struct fat_state *fs, const uint8_t *directory,
    const char name[11], struct fat_file *file)
{
	for (uint32_t offset = 0; offset < fs->io->sector; offset += 32) {
		const uint8_t *entry = directory + offset;
		if (entry[0] == 0)
			break;
		if (entry[0] == 0xe5 || (entry[11] & 0x18))
			continue;
		if (memcmp(entry, name, 11) == 0) {
			file->dir_offset = offset;
			file->first_cluster = (uint32_t)le16(entry + 20) << 16 |
			    le16(entry + 26);
			if (file->first_cluster < 6 ||
			    file->first_cluster >= fs->layout.clusters + 2)
				return -EINVAL;
			return 0;
		}
	}
	return -ENOENT;
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
	const struct esp_file *source = change->source;
	if (!source->read || !source->bytes || source->bytes > UINT32_MAX)
		return -EINVAL;
	uint8_t sector[4096];
	uint32_t previous = 0;
	uint64_t offset = 0;
	for (uint32_t cluster = 6; cluster < fs->layout.clusters + 2; cluster++) {
		if (fat_value(fs, cluster) != 0)
			continue;
		size_t amount = source->bytes - offset > fs->io->sector
		    ? fs->io->sector : (size_t)(source->bytes - offset);
		memset(sector, 0, fs->io->sector);
		int error = source->read(source->context, offset, sector, amount);
		if (error)
			return error;
		error = write_verify(fs->io, cluster_offset(fs, cluster), sector,
		    fs->io->sector);
		if (error)
			return error;
		fat_set(fs, cluster, EOC);
		if (previous)
			fat_set(fs, previous, cluster);
		else
			change->new_first = cluster;
		previous = cluster;
		offset += amount;
		if (offset == source->bytes)
			return 0;
	}
	return -ENOSPC;
}

static void set_dir_entry(uint8_t *directory, const struct file_change *change)
{
	uint8_t *entry = directory + change->current.dir_offset;
	storage_le16(entry + 20, (uint16_t)(change->new_first >> 16));
	storage_le16(entry + 26, (uint16_t)change->new_first);
	storage_le32(entry + 28, (uint32_t)change->source->bytes);
}

static int config_read(void *context, uint64_t offset, void *buffer, size_t size)
{
	memcpy(buffer, (const char *)context + offset, size);
	return 0;
}

int esp_update_boot(const struct storage_io *io, const struct esp_file *kernel,
    const struct esp_file *initramfs, const uint8_t root_uuid[16])
{
	struct fat_state fs = {0};
	char config[512];
	uint8_t boot_dir[4096], efi_dir[4096];
	if (!io || !io->read || !kernel || !initramfs || !root_uuid)
		return -EINVAL;
	int length = esp_limine_config(config, sizeof(config), root_uuid);
	if (length < 0)
		return length;
	struct esp_file config_file = {
	    .context = config, .bytes = (uint64_t)length, .read = config_read};
	struct file_change kernel_change = {.source = kernel};
	struct file_change init_change = {.source = initramfs};
	struct file_change config_change = {.source = &config_file};
	int error = fat_open(&fs, io);
	if (error)
		return error;

	uint64_t boot_offset = cluster_offset(&fs, 5);
	uint64_t efi_offset = cluster_offset(&fs, 4);
	if ((error = read_at(io, boot_offset, boot_dir, io->sector)) ||
	    (error = read_at(io, efi_offset, efi_dir, io->sector)) ||
	    (error = find_short(&fs, boot_dir, "KERNEL  ELF", &kernel_change.current)) ||
	    (error = find_short(&fs, boot_dir, "INIT    CPI", &init_change.current)) ||
	    (error = find_short(&fs, efi_dir, "LIMINE~1CNF", &config_change.current)))
		goto out;

	/* Keep the current files allocated until the new versions are published. */
	if ((error = write_new_file(&fs, &kernel_change)) ||
	    (error = write_new_file(&fs, &init_change)) ||
	    (error = write_new_file(&fs, &config_change)) ||
	    (error = io->flush(io->context)) ||
	    (error = fat_flush(&fs)))
		goto out;

	/* Publish the kernel and initramfs together in their shared sector. */
	set_dir_entry(boot_dir, &kernel_change);
	set_dir_entry(boot_dir, &init_change);
	if ((error = write_verify(io, boot_offset, boot_dir, io->sector)) ||
	    (error = io->flush(io->context)))
		goto out;
	set_dir_entry(efi_dir, &config_change);
	if ((error = write_verify(io, efi_offset, efi_dir, io->sector)) ||
	    (error = io->flush(io->context)))
		goto out;

	if ((error = old_chain(&fs, &kernel_change.current)) ||
	    (error = old_chain(&fs, &init_change.current)) ||
	    (error = old_chain(&fs, &config_change.current)))
		goto out;
	error = fat_flush(&fs);
out:
	free(fs.fat);
	return error;
}
