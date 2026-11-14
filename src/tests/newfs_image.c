#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../user/storage/storage.h"
static FILE *disk;
static unsigned writes, fail_at, flushes, fail_flush;
static int
write_image(void *c, uint64_t offset, const void *p, size_t n)
{
	(void)c;
	if (++writes == fail_at)
		return -EIO;
	assert(offset + n <= UINT64_C(4096) * UINT32_MAX);
	if (fseek(disk, (long)offset, SEEK_SET) != 0 ||
	    fwrite(p, 1, n, disk) != n)
		return -EIO;
	return 0;
}
static int
read_image(void *c, uint64_t offset, void *p, size_t n)
{
	(void)c;
	if (fseek(disk, (long)offset, SEEK_SET) != 0 ||
	    fread(p, 1, n, disk) != n)
		return -EIO;
	return 0;
}
static int
flush_image(void *c)
{
	(void)c;
	if (++flushes == fail_flush)
		return -EIO;
	return fflush(disk) == 0 ? 0 : -EIO;
}
int
main(int argc, char **argv)
{
	assert(argc == 6);
	uint64_t bytes = strtoull(argv[2], NULL, 10) * 4096;
	uint32_t inodes = (uint32_t)strtoul(argv[3], NULL, 10);
	fail_at = (unsigned)atoi(argv[4]);
	fail_flush = (unsigned)atoi(argv[5]);
	disk = fopen(argv[1], "r+b");
	assert(disk);
	struct storage_io io = {.bytes = bytes,
	    .sector = 512,
	    .read = read_image,
	    .write = write_image,
	    .flush = flush_image};
	struct sfs_layout plan;
	assert(sfs_plan(&io, inodes, &plan) == 0 && writes == 0);
	int result = sfs_format(&io, inodes);
	assert(result == ((fail_at || fail_flush) ? -EIO : 0));
	if (fail_at > 1 || (fail_flush && fail_flush < 3)) {
		unsigned char magic[8];
		assert(read_image(NULL, 0, magic, sizeof(magic)) == 0);
		assert(memcmp(magic, "SILFS1\0\0", 8) != 0);
	}
	struct storage_io invalid = io;
	invalid.bytes--;
	unsigned before = writes;
	assert(sfs_format(&invalid, inodes) == -EINVAL && writes == before);
	invalid = io;
	invalid.bytes = 4096;
	assert(sfs_format(&invalid, UINT32_MAX) == -ENOSPC && writes == before);
	fclose(disk);
}
