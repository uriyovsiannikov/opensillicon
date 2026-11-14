#include <string.h>

#include "kernel/block.h"
#include "kernel/errno.h"

static struct block_device *devices[BLOCK_DEVICE_MAX];
static size_t device_count;

int
block_device_register(struct block_device *device)
{
	size_t i;

	if (device == NULL || device->name[0] == '\0' ||
	    memchr(device->name, '\0', sizeof(device->name)) == NULL ||
	    (device->logical_sector_size != 512 &&
	    device->logical_sector_size != 4096) || device->sector_count == 0 ||
	    device->sector_count > UINT64_MAX / device->logical_sector_size ||
	    device->operations == NULL || device->operations->read == NULL ||
	    device->operations->write == NULL || device->registered)
		return -EINVAL;
	for (i = 0; i < device_count; i++)
		if (devices[i] == device || strcmp(devices[i]->name, device->name) == 0)
			return -EEXIST;
	if (device_count == BLOCK_DEVICE_MAX)
		return -ENOSPC;
	devices[device_count++] = device;
	device->registered = true;
	return 0;
}

size_t
block_device_count(void)
{
	return device_count;
}

struct block_device *
block_device_at(size_t index)
{
	return index < device_count ? devices[index] : NULL;
}

static int
validate_io(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, const void *buffer)
{
	if (device == NULL || !device->registered || buffer == NULL ||
	    sector_count == 0 || ((uintptr_t)buffer % device->logical_sector_size) != 0)
		return -EINVAL;
	if (first_sector >= device->sector_count ||
	    sector_count > device->sector_count - first_sector)
		return -EINVAL;
	if ((uint64_t)sector_count > SIZE_MAX / device->logical_sector_size)
		return -EOVERFLOW;
	return 0;
}

int
block_read(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, void *buffer)
{
	int error = validate_io(device, first_sector, sector_count, buffer);
	if (error != 0)
		return error;
	error = device->operations->read(device, first_sector, sector_count, buffer);
	return error <= 0 ? error : -EIO;
}

int
block_write(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, const void *buffer)
{
	int error = validate_io(device, first_sector, sector_count, buffer);
	if (error != 0)
		return error;
	error = device->operations->write(device, first_sector, sector_count, buffer);
	return error <= 0 ? error : -EIO;
}

int
block_flush(struct block_device *device)
{
	int error;

	if (device == NULL || !device->registered)
		return -EINVAL;
	if (device->operations->flush == NULL)
		return -EOPNOTSUPP;
	error = device->operations->flush(device);
	return error <= 0 ? error : -EIO;
}

static int
partition_read(struct block_device *device, uint64_t sector, uint32_t count,
	void *buffer)
{
	return block_read(device->parent, device->first_sector + sector, count, buffer);
}

static int
partition_write(struct block_device *device, uint64_t sector, uint32_t count,
	const void *buffer)
{
	return block_write(device->parent, device->first_sector + sector, count, buffer);
}

static int
partition_flush(struct block_device *device)
{
	return block_flush(device->parent);
}

static const struct block_device_operations partition_operations = {
	.read = partition_read, .write = partition_write, .flush = partition_flush,
};

int
block_partition_register(struct block_device *partition,
	struct block_device *parent, uint64_t first_sector, uint64_t sector_count,
	uint32_t number)
{
	char digits[10];
	size_t length = 0, used;
	if (partition == NULL || parent == NULL || !parent->registered ||
	    parent->parent != NULL || number == 0 || sector_count == 0 ||
	    first_sector >= parent->sector_count ||
	    sector_count > parent->sector_count - first_sector || partition->registered)
		return -EINVAL;
	*partition = (struct block_device){
		.logical_sector_size = parent->logical_sector_size,
		.sector_count = sector_count, .parent = parent,
		.first_sector = first_sector, .partition_number = number,
		.operations = &partition_operations,
	};
	used = strlen(parent->name);
	uint32_t value = number;
	do { digits[length++] = (char)('0' + value % 10); value /= 10; } while (value);
	if (used + length + 1 > BLOCK_DEVICE_NAME_MAX) return -ENAMETOOLONG;
	memcpy(partition->name, parent->name, used);
	partition->name[used++] = 'p';
	while (length) partition->name[used++] = digits[--length];
	partition->name[used] = 0;
	return block_device_register(partition);
}

static bool
ancestor(const struct block_device *parent, const struct block_device *child)
{
	return parent == child || child->parent == parent;
}

bool
block_raw_write_allowed(const struct block_device *device)
{
	if (device == NULL || !device->registered) return false;
	for (size_t i = 0; i < device_count; i++)
		if (devices[i]->mounted &&
		    (ancestor(device, devices[i]) || ancestor(devices[i], device)))
			return false;
	return true;
}
