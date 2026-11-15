#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/block.h"
#include "kernel/mount.h"
#include "kernel/vfs.h"
#include "kernel/fd.h"
#include "kernel/errno.h"
static unsigned int reads;
static size_t live, budget = SIZE_MAX;
static bool busy_directory, fail_flush;
void *
kheap_alloc(size_t n)
{
	if (!budget)
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
klog(const char *s)
{
	fputs(s, stderr);
}
_Noreturn void
kernel_halt(void)
{
	abort();
}
bool
process_directory_busy(const char *p)
{
	(void)p;
	return busy_directory;
}
static int
read_disk(struct block_device *d, uint64_t s, uint32_t n, void *p)
{
	FILE *f = d->context;
	reads++;
	return fseek(f, (long)(s * d->logical_sector_size), SEEK_SET) == 0 &&
		fread(p, d->logical_sector_size, n, f) == n
	    ? 0
	    : -EIO;
}
static int
write_disk(struct block_device *d, uint64_t s, uint32_t n, const void *p)
{
	FILE *f = d->context;
	return fseek(f, (long)(s * d->logical_sector_size), SEEK_SET) == 0 &&
		fwrite(p, d->logical_sector_size, n, f) == n
	    ? 0
	    : -EIO;
}
static int
flush_disk(struct block_device *d)
{
	return fail_flush || fflush(d->context) != 0 ? -EIO : 0;
}
static const struct block_device_operations ops = {
    read_disk, write_disk, flush_disk};
static void
directory(const char *path)
{
	struct vnode *v;
	assert(vfs_create_path(path, VNODE_DIRECTORY, 0700, &v) == 0);
	vnode_put(v);
}

/* Data used by the large-file test: a cheap position-dependent pattern so a
 * multi-megabyte file needs no backing store on the host. */
static unsigned char chunk[64 * 1024];

static unsigned char
pattern_byte(uint64_t offset)
{
	return (unsigned char)(offset ^ (offset >> 8) ^ (offset >> 16) ^
	    (offset >> 24));
}

static void
write_file(struct fd_object *file, size_t length)
{
	size_t n;

	for (size_t offset = 0; offset < length;) {
		size_t amount = length - offset < sizeof(chunk) ?
		    length - offset : sizeof(chunk);
		for (size_t i = 0; i < amount; i++)
			chunk[i] = pattern_byte(offset + i);
		assert(fd_object_write(file, chunk, amount, &n) == 0 &&
		    n == amount);
		offset += amount;
	}
}

static void
verify_file(struct fd_object *file, size_t length)
{
	size_t n;

	for (size_t offset = 0; offset < length;) {
		size_t amount = length - offset < sizeof(chunk) ?
		    length - offset : sizeof(chunk);
		assert(file->operations->read(file->context, offset, chunk,
		    amount, &n) == 0 && n == amount);
		for (size_t i = 0; i < amount; i++)
			assert(chunk[i] == pattern_byte(offset + i));
		offset += amount;
	}
	char extra;
	assert(file->operations->read(file->context, length, &extra, 1, &n)
	    == 0 && n == 0);
}

static void
truncate_path(const char *path, uint64_t size)
{
	struct vnode *node;

	assert(vfs_lookup(path, &node) == 0);
	assert(node->operations->truncate(node, size) == 0);
	vnode_put(node);
}

static void
verify_size(const char *path, uint64_t size)
{
	struct vfs_stat status;

	assert(vfs_stat_path(path, &status) == 0 && status.size == size);
}
int
main(int argc, char **argv)
{
	assert(argc == 3);
	FILE *f = fopen(argv[1], "r+b");
	assert(f);
	assert(fseek(f, 0, SEEK_END) == 0);
	uint64_t bytes = (uint64_t)ftell(f);
	struct block_device disk = {.name = "ada0",
	    .sector_count = bytes / 512,
	    .logical_sector_size = 512,
	    .operations = &ops,
	    .context = f};
	FILE *second = fopen(argv[2], "r+b");
	assert(second);
	struct block_device disk2 = disk;
	strcpy(disk2.name, "ada1");
	disk2.context = second;
	assert(block_device_register(&disk) == 0 &&
	    block_device_register(&disk2) == 0 && vfs_init());
	struct vnode *root = ramfs_create_root();
	assert(root);
	assert(vfs_mount("/", root) == 0);
	vnode_put(root);
	directory("/mnt");
	assert(filesystem_mount("/dev/ada0", "/mnt/") == 0 && disk.mounted &&
	    disk.references == 2);
	/* 128 table blocks, regardless of how many inode records they hold. */
	printf("SilliconFS mount reads for 4096 inodes: %u\n", reads);
	assert(reads < 150);
	assert(!block_raw_write_allowed(&disk));
	struct fd_object *file;
	size_t n;
	assert(
	    vfs_open("/mnt/probe", VFS_O_RDWR | VFS_O_CREAT, 0640, &file) == 0);
	assert(fd_object_write(file, "durable", 7, &n) == 0 && n == 7);
	assert(file->operations->sync(file->context) == 0);
	assert(filesystem_unmount("/mnt") == -EBUSY);
	fd_object_put(file);
	directory("/mnt/sub");
	assert(filesystem_mount("/dev/ada1", "/mnt/sub") == 0);
	assert(filesystem_unmount("/mnt") == -EBUSY);
	assert(filesystem_unmount("/mnt/sub") == 0 && !disk2.mounted &&
	    disk2.references == 1);
	busy_directory = true;
	assert(filesystem_unmount("/mnt") == -EBUSY);
	busy_directory = false;
	fail_flush = true;
	assert(filesystem_sync_all() == -EIO);
	assert(filesystem_unmount("/mnt") == -EIO && disk.mounted);
	fail_flush = false;
	assert(filesystem_unmount("/mnt/") == 0 && !disk.mounted &&
	    disk.references == 1);
	assert(filesystem_mount("/dev/ada0", "/mnt") == 0);
	assert(vfs_open("/mnt/probe", VFS_O_RDONLY, 0, &file) == 0);
	char data[8] = {0};
	assert(fd_object_read(file, data, 7, &n) == 0 && n == 7 &&
	    strcmp(data, "durable") == 0);
	fd_object_put(file);

	/* An ELF-sized read crosses the direct/indirect boundary and EOF.
	 * The allocation bitmap and indirect block should each be read once. */
	static unsigned char payload[48 * 4096 + 111], loaded[sizeof(payload)];
	for (size_t i = 0; i < sizeof(payload); i++)
		payload[i] = (unsigned char)(i ^ (i >> 12));
	assert(vfs_open("/mnt/program", VFS_O_RDWR | VFS_O_CREAT, 0755,
	    &file) == 0);
	assert(fd_object_write(file, payload, sizeof(payload), &n) == 0 &&
	    n == sizeof(payload));
	reads = 0;
	assert(file->operations->read(file->context, 37, loaded, sizeof(loaded),
	    &n) == 0 && n == sizeof(payload) - 37);
	assert(memcmp(payload + 37, loaded, n) == 0);
	printf("SilliconFS reads for a file spanning 49 blocks: %u\n", reads);
	assert(reads == 53); /* inode map/table, data map, indirect table and data */
	fd_object_put(file);

	/* Past 1036 blocks the inode switches to the double-indirect table.
	 * A remount re-validates every block table, so this also proves the
	 * nested tables are reachable and not double-referenced. */
	const size_t large_size = 5 * 1024 * 1024 + 123;
	assert(vfs_open("/mnt/large", VFS_O_RDWR | VFS_O_CREAT, 0644, &file) == 0);
	write_file(file, large_size);
	fd_object_put(file);
	verify_size("/mnt/large", large_size);
	assert(filesystem_unmount("/mnt") == 0);
	assert(filesystem_mount("/dev/ada0", "/mnt") == 0);
	assert(vfs_open("/mnt/large", VFS_O_RDONLY, 0, &file) == 0);
	verify_file(file, large_size);
	fd_object_put(file);

	/* Shrinking inside the double-indirect range keeps the tables that
	 * still hold data and trims the last one. */
	const size_t partial_size = 4 * 1024 * 1024 + 100000;
	truncate_path("/mnt/large", partial_size);
	verify_size("/mnt/large", partial_size);
	assert(vfs_open("/mnt/large", VFS_O_RDONLY, 0, &file) == 0);
	verify_file(file, partial_size);
	fd_object_put(file);
	assert(filesystem_unmount("/mnt") == 0);
	assert(filesystem_mount("/dev/ada0", "/mnt") == 0);
	assert(vfs_open("/mnt/large", VFS_O_RDONLY, 0, &file) == 0);
	verify_file(file, partial_size);
	fd_object_put(file);

	/* Shrinking below the single-indirect range releases every table. */
	const size_t small_size = 3 * 1024 * 1024 + 7;
	truncate_path("/mnt/large", small_size);
	verify_size("/mnt/large", small_size);
	assert(vfs_open("/mnt/large", VFS_O_RDONLY, 0, &file) == 0);
	verify_file(file, small_size);
	fd_object_put(file);

	/* Removing the file releases both table levels, and their blocks are
	 * immediately reusable by another large file. */
	assert(vfs_remove_path_as("/mnt/large", false, 0, 0) == 0);
	assert(vfs_open("/mnt/reuse", VFS_O_RDWR | VFS_O_CREAT, 0644, &file) == 0);
	write_file(file, large_size);
	fd_object_put(file);
	assert(vfs_open("/mnt/reuse", VFS_O_RDONLY, 0, &file) == 0);
	verify_file(file, large_size);
	fd_object_put(file);
	assert(vfs_remove_path_as("/mnt/reuse", false, 0, 0) == 0);

	/* A sparse write past 4 MiB allocates only the tables it touches. */
	const uint64_t sparse_offset = 5 * 1024 * 1024;
	static unsigned char hole[4096];
	assert(vfs_open("/mnt/sparse", VFS_O_RDWR | VFS_O_CREAT, 0644, &file) == 0);
	assert(file->operations->write(file->context, sparse_offset, "tail", 4,
	    &n) == 0 && n == 4);
	fd_object_put(file);
	verify_size("/mnt/sparse", sparse_offset + 4);
	assert(filesystem_unmount("/mnt") == 0);
	assert(filesystem_mount("/dev/ada0", "/mnt") == 0);
	assert(vfs_open("/mnt/sparse", VFS_O_RDONLY, 0, &file) == 0);
	assert(file->operations->read(file->context, sparse_offset - 4, hole, 8,
	    &n) == 0 && n == 8);
	assert(hole[0] == 0 && hole[1] == 0 && hole[2] == 0 && hole[3] == 0 &&
	    memcmp(hole + 4, "tail", 4) == 0);
	fd_object_put(file);
	assert(vfs_remove_path_as("/mnt/sparse", false, 0, 0) == 0);

	assert(filesystem_unmount("/mnt") == 0);
	size_t baseline = live;
	for (size_t i = 0; i < 24; i++) {
		budget = i;
		int result = filesystem_mount("/dev/ada0", "/mnt");
		budget = SIZE_MAX;
		if (!result)
			assert(filesystem_unmount("/mnt") == 0);
		else
			assert(result == -ENOMEM);
		assert(
		    !disk.mounted && disk.references == 1 && live == baseline);
	}
	assert(filesystem_unmount("/") == -EINVAL);
	/* A nonzero free inode must still be detected in the last table block. */
	long unused_inode = 3 * 4096L + 4095 * 128L;
	assert(fseek(f, unused_inode, SEEK_SET) == 0 && fputc('X', f) != EOF &&
	    fflush(f) == 0);
	assert(filesystem_mount("/dev/ada0", "/mnt") == -EIO && live == baseline);
	assert(fseek(f, unused_inode, SEEK_SET) == 0 && fputc(0, f) != EOF &&
	    fflush(f) == 0);
	/* The reserved metadata bits are still checked. */
	assert(fseek(f, 4096, SEEK_SET) == 0 && fputc(254, f) != EOF &&
	    fflush(f) == 0);
	assert(filesystem_mount("/dev/ada0", "/mnt") == -EIO && live == baseline);
	assert(fseek(f, 4096, SEEK_SET) == 0 && fputc(255, f) != EOF &&
	    fflush(f) == 0);
	assert(fseek(f, 0, SEEK_SET) == 0 && fputc('X', f) != EOF &&
	    fflush(f) == 0);
	assert(filesystem_mount("/dev/ada0", "/mnt") == -EIO && !disk.mounted &&
	    disk.references == 1 && live == baseline);
	fclose(f);
	fclose(second);
	puts(
	    "temporary SilliconFS mounts: open-file/CWD protection, fsync, flush failure and allocation rollback: PASS");
}
