#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/gpt.h"
#include "kernel/errno.h"
static size_t budget = SIZE_MAX, live;
void *
kheap_alloc(size_t n)
{
	if (budget == 0)
		return NULL;
	if (budget != SIZE_MAX)
		budget--;
	void *p = malloc(n);
	if (p)
		live++;
	return p;
}
bool
kheap_free(void *p)
{
	if (p) {
		assert(live);
		live--;
	}
	free(p);
	return true;
}
void
klog(const char *p)
{
	(void)p;
}
static int
read_disk(struct block_device *d, uint64_t s, uint32_t n, void *p)
{
	FILE *f = d->context;
	if (fseek(f, (long)(s * d->logical_sector_size), SEEK_SET) != 0)
		return -EIO;
	return fread(p, d->logical_sector_size, n, f) == n ? 0 : -EIO;
}
static int
write_disk(struct block_device *d, uint64_t s, uint32_t n, const void *p)
{
	(void)d;
	(void)s;
	(void)n;
	(void)p;
	abort();
}
static int
flush_disk(struct block_device *d)
{
	(void)d;
	abort();
}
static const struct block_device_operations ops = {
    read_disk, write_disk, flush_disk};
int
main(int argc, char **argv)
{
	assert(argc == 5);
	FILE *f = fopen(argv[1], "rb");
	assert(f);
	struct block_device disk = {.name = "ada0",
	    .logical_sector_size = (uint32_t)atoi(argv[2]),
	    .sector_count = 4096,
	    .operations = &ops,
	    .context = f};
	assert(block_device_register(&disk) == 0);
	struct gpt_table *table = malloc(sizeof(*table));
	assert(table);
	int result = gpt_read(&disk, table), expected = atoi(argv[3]);
	assert(expected < 0 ? result < 0 : result == expected);
	if (result > 0) {
		assert(table->degraded == (atoi(argv[4]) != 0));
		assert(table->backup == (atoi(argv[4]) == 2));
		assert(table->partitions[0].number == 1 &&
		    table->partitions[1].number == 3);
		assert(table->partitions[0].first == 128 &&
		    table->partitions[0].last == 255);
		gpt_discover();
		assert(block_device_count() == 3);
		assert(strcmp(block_device_at(2)->name, "ada0p3") == 0);
		gpt_discover();
		assert(block_device_count() == 3);
		for (size_t limit = 0; limit < 11; limit++) {
			struct block_device *old = block_device_at(1);
			assert(block_device_get(old));
			size_t before = live;
			budget = limit;
			int status = gpt_rescan(&disk);
			budget = SIZE_MAX;
			if (status) {
				assert(old->registered &&
				    block_device_at(1) == old &&
				    live == before);
			} else {
				assert(!old->registered &&
				    block_device_at(1) != old &&
				    live == before + 1);
			}
			block_device_put(old);
			assert(live == 2);
		}
		block_device_at(1)->mounted = true;
		assert(gpt_rescan(&disk) == -EBUSY && live == 2);
		block_device_at(1)->mounted = false;
		static _Alignas(4096) unsigned char data[4096];
		assert(block_read(block_device_at(1), 128, 1, data) == -EINVAL);
		assert(block_replace_partitions(&disk, NULL, 0) == 0 &&
		    live == 0 && disk.references == 1);
	} else {
		gpt_discover();
		assert(block_device_count() == 1);
	}
	free(table);
	fclose(f);
}
