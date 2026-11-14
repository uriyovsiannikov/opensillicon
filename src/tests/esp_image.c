#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "../user/storage/storage.h"
static FILE *image;
static unsigned writes, flushes, fail_write, fail_flush;
static uint64_t media;
static int
image_read(void *c, uint64_t o, void *p, size_t n)
{
	(void)c;
	assert(o <= media && n <= media - o);
	return fseek(image, (long)o, SEEK_SET) == 0 &&
		fread(p, 1, n, image) == n
	    ? 0
	    : -EIO;
}
static int
image_write(void *c, uint64_t o, const void *p, size_t n)
{
	(void)c;
	assert(o <= media && n <= media - o);
	if (++writes == fail_write)
		return -EIO;
	return fseek(image, (long)o, SEEK_SET) == 0 &&
		fwrite(p, 1, n, image) == n
	    ? 0
	    : -EIO;
}
static int
flush_image(void *c)
{
	(void)c;
	if (++flushes == fail_flush)
		return -EIO;
	return fflush(image) == 0 ? 0 : -EIO;
}
static int
file_read(void *c, uint64_t o, void *p, size_t n)
{
	return fseek(c, (long)o, SEEK_SET) == 0 && fread(p, 1, n, c) == n
	    ? 0
	    : -EIO;
}
int
main(int argc, char **argv)
{
	assert(argc == 9);
	image = fopen(argv[1], "r+b");
	assert(image);
	assert(fseek(image, 0, SEEK_END) == 0);
	media = (uint64_t)ftell(image);
	struct storage_io io = {.bytes = media,
	    .sector = (uint32_t)atoi(argv[2]),
	    .read = image_read,
	    .write = image_write,
	    .flush = flush_image};
	struct esp_file files[4];
	for (unsigned i = 0; i < 4; i++) {
		FILE *f = fopen(argv[3 + i], "rb");
		assert(f && fseek(f, 0, SEEK_END) == 0);
		files[i] = (struct esp_file){.context = f,
		    .bytes = (uint64_t)ftell(f),
		    .read = file_read};
	}
	fail_write = (unsigned)atoi(argv[7]);
	fail_flush = (unsigned)atoi(argv[8]);
	struct fat32_layout l;
	assert(fat32_plan(&io, &l) == 0 && writes == 0);
	struct storage_io bad = io;
	bad.bytes = 16 * 1024 * 1024;
	assert(fat32_plan(&bad, &l) == -ENOSPC && writes == 0);
	int result = esp_format(&io, 0, 0x12345678, files);
	assert(result == ((fail_write || fail_flush) ? -EIO : 0));
	for (unsigned i = 0; i < 4; i++)
		fclose(files[i].context);
	fclose(image);
}
