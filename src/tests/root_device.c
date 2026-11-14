#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common/uuid.h"
#include "kernel/root_device.h"
#include "kernel/errno.h"
static int
read_disk(struct block_device *d, uint64_t s, uint32_t n, void *p)
{
	(void)d;
	(void)s;
	(void)n;
	(void)p;
	return -EIO;
}
static int
write_disk(struct block_device *d, uint64_t s, uint32_t n, const void *p)
{
	(void)d;
	(void)s;
	(void)n;
	(void)p;
	return -EIO;
}
static const struct block_device_operations ops = {
    .read = read_disk, .write = write_disk};
int
main(void)
{
	const char *id = "00112233-4455-6677-8899-aabbccddeeff";
	uint8_t wire[16];
	char text[37];
	assert(uuid_parse(id, 36, wire));
	assert(wire[0] == 0x33 && wire[4] == 0x55 && wire[6] == 0x77 &&
	    wire[15] == 0xff);
	uuid_format(wire, text);
	assert(strcmp(text, id) == 0);
	assert(!uuid_parse("00112233-4455-6677-8899-aabbccddeefg", 36, wire));
	assert(!uuid_parse(id, 35, wire));
	struct block_device disk = {.name = "ada0",
	    .sector_count = 4096,
	    .logical_sector_size = 512,
	    .operations = &ops};
	struct block_device a = {0}, b = {0}, *selected;
	assert(block_device_register(&disk) == 0);
	assert(block_partition_register(&a, &disk, 64, 64, 1) == 0);
	assert(block_partition_register(&b, &disk, 128, 64, 2) == 0);
	assert(uuid_parse(id, 36, a.partition_uuid));
	assert(root_device_select(NULL, &selected) == 0 && selected == NULL);
	assert(
	    root_device_select(
		"nosmp root=PARTUUID=00112233-4455-6677-8899-AABBCCDDEEFF other",
		&selected) == 1 &&
	    selected == &a);
	assert(root_device_select(
		   "root=PARTUUID=00112233-4455-6677-8899-aabbccddee00",
		   &selected) == -2);
	assert(root_device_select("root=/dev/ada0p1", &selected) == -EINVAL);
	assert(
	    root_device_select(
		"root=PARTUUID=00112233-4455-6677-8899-aabbccddeeff root=PARTUUID=00112233-4455-6677-8899-aabbccddeeff",
		&selected) == -EINVAL);
	memcpy(b.partition_uuid, a.partition_uuid, 16);
	assert(root_device_select(
		   "root=PARTUUID=00112233-4455-6677-8899-aabbccddeeff",
		   &selected) == -EEXIST &&
	    selected == NULL);
	puts(
	    "root PARTUUID: wire byte order, strict parsing, uniqueness and failure behavior: PASS");
}
