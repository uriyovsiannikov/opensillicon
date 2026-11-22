#include <assert.h>
#include <stdio.h>

#include "kernel/block.h"
#include "kernel/errno.h"

struct flush_state { unsigned calls; int error; };

static int
read_unused(struct block_device *device, uint64_t sector, uint32_t count, void *buffer)
{
	(void)device; (void)sector; (void)count; (void)buffer;
	return -EIO;
}

static int
write_unused(struct block_device *device, uint64_t sector, uint32_t count, const void *buffer)
{
	(void)device; (void)sector; (void)count; (void)buffer;
	return -EIO;
}

static int
flush(struct block_device *device)
{
	struct flush_state *state = device->context;
	state->calls++;
	return state->error;
}

int
main(void)
{
	static const struct block_device_operations operations = {
		.read = read_unused, .write = write_unused, .flush = flush
	};
	struct flush_state states[3] = {0};
	struct block_device disks[3] = {0}, partitions[2] = {0};
	assert(block_flush_all() == 0);
	for (unsigned i = 0; i < 3; i++) {
		snprintf(disks[i].name, sizeof(disks[i].name), "disk%u", i);
		disks[i].logical_sector_size = 512;
		disks[i].sector_count = 64;
		disks[i].operations = &operations;
		disks[i].context = &states[i];
		assert(block_device_register(&disks[i]) == 0);
	}
	for (unsigned i = 0; i < 2; i++) {
		assert(block_partition_prepare(&partitions[i], &disks[0], 1 + 16*i, 16, i+1) == 0);
		assert(block_device_register(&partitions[i]) == 0);
	}
	partitions[0].mounted = true;
	assert(block_flush_all() == 0);
	for (unsigned i = 0; i < 3; i++) assert(states[i].calls == 1);
	states[0].error = -EIO;
	states[1].error = -ENOSPC;
	assert(block_flush_all() == -EIO);
	for (unsigned i = 0; i < 3; i++) assert(states[i].calls == 2);
	states[0].error = 0;
	assert(block_flush_all() == -ENOSPC);
	for (unsigned i = 0; i < 3; i++) assert(states[i].calls == 3);
	puts("BLOCK_FLUSH_PASS: unmounted disks, partition deduplication and error continuation");
	return 0;
}
