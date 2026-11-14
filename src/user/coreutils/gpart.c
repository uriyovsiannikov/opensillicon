#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include "../storage/storage.h"
static int
usage(void)
{
	fputs(
	    "usage: gpart bootlayout [-N | -F] [-e esp-MiB] /dev/ada0\n"
	    "  Creates EFI + SilliconFS partitions; -N previews, -F replaces disk partition tables.\n",
	    stderr);
	return 2;
}
static int
uuids(uint8_t ids[48])
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return -1;
	size_t done = 0;
	while (done < 48) {
		ssize_t n = read(fd, ids + done, 48 - done);
		if (n <= 0) {
			close(fd);
			return -1;
		}
		done += (size_t)n;
	}
	close(fd);
	for (unsigned i = 0; i < 3; i++) {
		ids[i * 16 + 7] = (ids[i * 16 + 7] & 15) | 0x40;
		ids[i * 16 + 8] = (ids[i * 16 + 8] & 63) | 0x80;
	}
	return 0;
}
int
main(int argc, char **argv)
{
	int format = 0, mode_seen = 0;
	uint32_t esp_mib = 512;
	const char *path = NULL;
	if (argc < 3 || strcmp(argv[1], "bootlayout") != 0)
		return usage();
	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-N") == 0 || strcmp(argv[i], "-F") == 0) {
			if (mode_seen++)
				return usage();
			format = strcmp(argv[i], "-F") == 0;
		} else if (strcmp(argv[i], "-e") == 0) {
			if (++i == argc)
				return usage();
			uint64_t n = 0;
			const char *p = argv[i];
			if (!*p)
				return usage();
			for (; *p; p++) {
				if (*p < '0' || *p > '9' ||
				    n > (UINT32_MAX - (unsigned)(*p - '0')) /
					    10)
					return usage();
				n = n * 10 + (unsigned)(*p - '0');
			}
			esp_mib = (uint32_t)n;
		} else if (argv[i][0] == '-' || path)
			return usage();
		else
			path = argv[i];
	}
	if (!path || strncmp(path, "/dev/ada", 8) != 0 || !path[8])
		return usage();
	for (const char *p = path + 8; *p; p++)
		if (*p < '0' || *p > '9')
			return usage();
	struct storage_device device;
	int error = storage_open(&device, path, format);
	if (error) {
		fprintf(
		    stderr, "gpart: cannot open %s (errno %d)\n", path, -error);
		return 1;
	}
	struct gpt_layout plan;
	error = gpt_layout_plan(&device.io, esp_mib, &plan);
	if (!error) {
		printf("%s: %llu bytes, sector size %u\n", path,
		    (unsigned long long)device.io.bytes, device.io.sector);
		printf("p1 EFI: start %llu, sectors %llu\n",
		    (unsigned long long)plan.esp_first,
		    (unsigned long long)plan.esp_sectors);
		printf("p2 SilliconFS: start %llu, sectors %llu\n",
		    (unsigned long long)plan.root_first,
		    (unsigned long long)plan.root_sectors);
		if (format) {
			uint8_t ids[48];
			if (uuids(ids) != 0) {
				fputs(
				    "gpart: UUID randomness unavailable; disk was not changed\n",
				    stderr);
				error = -EAGAIN;
			} else {
				error =
				    gpt_layout_write(&device.io, esp_mib, ids);
				if (!error &&
				    ioctl(device.fd, DIOCREREADGPT) != 0) {
					error = -errno;
					fprintf(stderr,
					    "gpart: GPT written but kernel refresh failed; reboot before using partitions\n");
				}
			}
		} else
			puts(
			    "Preview only. Use -F to replace this disk's partition tables.");
	}
	int close_error = storage_close(&device);
	if (!error)
		error = close_error;
	if (error) {
		fprintf(stderr, "gpart: failed (errno %d)\n", -error);
		return 1;
	}
	if (format)
		puts(
		    "GPT written, flushed and verified. Partition devices are now available.");
	return 0;
}
