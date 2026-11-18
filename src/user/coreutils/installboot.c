#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "common/uuid.h"
#include "../storage/storage.h"
struct memory_file {
	const char *data;
	size_t bytes;
};
static int
memory_read(void *c, uint64_t offset, void *buffer, size_t n)
{
	struct memory_file *file = c;
	if (offset > file->bytes || n > file->bytes - offset)
		return -EINVAL;
	memcpy(buffer, file->data + offset, n);
	return 0;
}
static int
source_read(void *c, uint64_t offset, void *buffer, size_t n)
{
	int fd = *(int *)c;
	off_t position = lseek(fd, (off_t)offset, SEEK_SET);
	if (position != (off_t)offset)
		return position < 0 ? -errno : -EIO;
	size_t done = 0;
	while (done < n) {
		ssize_t result = read(fd, (uint8_t *)buffer + done, n - done);
		if (result < 0)
			return -errno;
		if (result == 0)
			return -EIO;
		done += (size_t)result;
	}
	return 0;
}
static int
usage(void)
{
	fputs(
	    "usage: installboot [-N | -F] -r root-PARTUUID /dev/ada0p1\n"
	    "  -N previews (default). -F formats the EFI partition and installs UEFI boot files.\n",
	    stderr);
	return 2;
}
static int
partition_path(const char *path)
{
	if (strncmp(path, "/dev/ada", 8))
		return 0;
	const char *p = path + 8;
	if (*p < '0' || *p > '9')
		return 0;
	while (*p >= '0' && *p <= '9')
		p++;
	if (*p++ != 'p' || *p < '1' || *p > '9')
		return 0;
	while (*p >= '0' && *p <= '9')
		p++;
	return *p == 0;
}
int
main(int argc, char **argv)
{
	int format = 0, mode_seen = 0;
	const char *path = NULL, *root = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-N") == 0 || strcmp(argv[i], "-F") == 0) {
			if (mode_seen++)
				return usage();
			format = strcmp(argv[i], "-F") == 0;
		} else if (strcmp(argv[i], "-r") == 0) {
			if (root || ++i == argc)
				return usage();
			root = argv[i];
		} else if (argv[i][0] == '-' || path)
			return usage();
		else
			path = argv[i];
	}
	uint8_t uuid[16];
	char id[37];
	if (!path || !partition_path(path) || !root ||
	    !uuid_parse(root, strlen(root), uuid))
		return usage();
	static const uint8_t zero[16];
	if (memcmp(uuid, zero, 16) == 0)
		return usage();
	uuid_format(uuid, id);
	char config[512];
	int length = snprintf(config, sizeof(config),
	    "timeout: 10\ndefault_entry: 1\n"
	    "/OpenSillicon A\n protocol: limine\n path: boot():/boot/kernelA.elf\n"
	    " module_path: boot():/boot/initA.cpi\n cmdline: root=PARTUUID=%s kernel-slot=A\n"
	    "/OpenSillicon B\n protocol: limine\n path: boot():/boot/kernelB.elf\n"
	    " module_path: boot():/boot/initB.cpi\n cmdline: root=PARTUUID=%s kernel-slot=B\n",
	    id, id);
	if (length < 0 || (size_t)length >= sizeof(config))
		return 1;
	struct memory_file memory = {config, (size_t)length};
	static const char *sources[] = {
	    "/boot/BOOTX64.EFI", "/boot/kernel.elf", "/boot/init.cpi"};
	static const unsigned slot[] = {0, 2, 3};
	int fds[3] = {-1, -1, -1}, error = 0;
	struct esp_file files[4] = {{0}};
	files[1] = (struct esp_file){
	    .context = &memory, .bytes = (uint64_t)length, .read = memory_read};
	for (unsigned i = 0; i < 3; i++) {
		fds[i] = open(sources[i], O_RDONLY);
		if (fds[i] < 0) {
			error = -errno;
			break;
		}
		struct stat st;
		if (fstat(fds[i], &st) != 0) {
			error = -errno;
			break;
		}
		if (!S_ISREG(st.st_mode) || st.st_size < 8 ||
		    (uint64_t)st.st_size > UINT32_MAX) {
			error = -EINVAL;
			break;
		}
		files[slot[i]] = (struct esp_file){.context = &fds[i],
		    .bytes = (uint64_t)st.st_size,
		    .read = source_read};
		uint8_t magic[8];
		error = source_read(&fds[i], 0, magic, sizeof(magic));
		if (error)
			break;
		if ((i == 0 && memcmp(magic, "MZ", 2)) ||
		    (i == 1 &&
			memcmp(magic,
			    "\x7f"
			    "ELF",
			    4)) ||
		    (i == 2 && memcmp(magic, "070701", 6) &&
			memcmp(magic, "070702", 6))) {
			error = -EINVAL;
			break;
		}
	}
	struct storage_device device = {.fd = -1};
	if (!error)
		error = storage_open(&device, path, format);
	struct os_disk_partinfo info;
	if (!error && ioctl(device.fd, DIOCGPARTINFO, &info) != 0)
		error = -errno;
	static const uint8_t esp_type[16] = {0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8,
	    0xd2, 0x11, 0xba, 0x4b, 0, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
	if (!error &&
	    (!(info.flags & OS_DISK_PARTITION) ||
		memcmp(info.type_uuid, esp_type, 16)))
		error = -EINVAL;
	if (!error && info.first_sector > UINT32_MAX)
		error = -EOVERFLOW;
	struct fat32_layout plan;
	if (!error)
		error = fat32_plan(&device.io, &plan);
	if (!error) {
		uint64_t needed = 4;
		for (unsigned i = 0; i < 4; i++)
			needed += (files[i].bytes + device.io.sector - 1) /
			    device.io.sector;
		if (needed > plan.clusters)
			error = -ENOSPC;
	}
	if (!error) {
		printf("%s: FAT32 EFI partition, %llu bytes, %u-byte sectors\n",
		    path, (unsigned long long)device.io.bytes,
		    device.io.sector);
		printf("Installed root: PARTUUID=%s\n", id);
		if (format)
			error =
			    esp_format(&device.io, (uint32_t)info.first_sector,
				storage_crc32(uuid, sizeof(uuid)), files);
		else
			puts(
			    "Preview only. Use -F to format the EFI partition and write the boot files.");
	}
	if (device.fd >= 0) {
		int result = storage_close(&device);
		if (!error)
			error = result;
	}
	for (unsigned i = 0; i < 3; i++)
		if (fds[i] >= 0)
			close(fds[i]);
	if (error) {
		fprintf(stderr, "installboot: failed (errno %d)\n", -error);
		return 1;
	}
	if (format)
		puts("UEFI boot files installed, flushed and verified.");
	return 0;
}
