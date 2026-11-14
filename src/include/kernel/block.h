#ifndef KERNEL_BLOCK_H
#define KERNEL_BLOCK_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define BLOCK_DEVICE_NAME_MAX 31U
#define BLOCK_DEVICE_MAX 128U

struct block_device;

struct block_device_operations {
	int (*read)(struct block_device *device, uint64_t first_sector,
	    uint32_t sector_count, void *buffer);
	int (*write)(struct block_device *device, uint64_t first_sector,
	    uint32_t sector_count, const void *buffer);
	int (*flush)(struct block_device *device);
};

struct block_device {
	char name[BLOCK_DEVICE_NAME_MAX + 1];
	char model[41], ident[21];
	uint32_t logical_sector_size;
	uint64_t sector_count;
	const struct block_device_operations *operations;
	void *context;
	bool registered;
	/* Raw device writes must not bypass a mounted filesystem. */
	bool mounted;
	struct block_device *parent;
	uint64_t first_sector;
	uint32_t partition_number;
	uint8_t partition_uuid[16];
	uint8_t partition_type[16];
	uint32_t references;
	void (*destroy)(struct block_device *);
};

bool block_device_get(struct block_device *device);
void block_device_put(struct block_device *device);
int block_replace_partitions(
    struct block_device *parent, struct block_device **parts, size_t count);
int block_partition_prepare(struct block_device *partition,
    struct block_device *parent, uint64_t first_sector, uint64_t sector_count,
    uint32_t number);
bool block_raw_write_allowed(const struct block_device *device);
int block_partition_register(struct block_device *partition,
    struct block_device *parent, uint64_t first_sector, uint64_t sector_count,
    uint32_t number);
int block_device_register(struct block_device *device);
size_t block_device_count(void);
struct block_device *block_device_at(size_t index);
int block_read(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, void *buffer);
int block_write(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, const void *buffer);
int block_flush(struct block_device *device);

#endif
