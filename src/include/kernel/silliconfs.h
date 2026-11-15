#ifndef KERNEL_SILLICONFS_H
#define KERNEL_SILLICONFS_H

#include <stdint.h>

#include "kernel/vfs.h"

#define SILLICONFS_BLOCK_SIZE 4096U

/* Inode block map: 12 direct entries, one single-indirect table and one
 * double-indirect table, each table holding SILLICONFS_BLOCK_SIZE / 4 entries. */
#define SILLICONFS_DIRECT_BLOCKS 12U
#define SILLICONFS_INDIRECT_BLOCKS (SILLICONFS_BLOCK_SIZE / 4U)
#define SILLICONFS_MAX_FILE_BLOCKS \
	((uint64_t)SILLICONFS_DIRECT_BLOCKS + SILLICONFS_INDIRECT_BLOCKS + \
	(uint64_t)SILLICONFS_INDIRECT_BLOCKS * SILLICONFS_INDIRECT_BLOCKS)
#define SILLICONFS_MAX_FILE_SIZE \
	(SILLICONFS_MAX_FILE_BLOCKS * SILLICONFS_BLOCK_SIZE)

struct silliconfs_device {
	void *context;
	uint32_t block_count;
	int (*flush)(void *context);
	int (*read_block)(void *context, uint32_t block_number, void *block);
	int (*write_block)(void *context, uint32_t block_number,
	    const void *block);
};

/* Returns zero and a referenced root vnode on success, negative errno on error. */
int silliconfs_mount(const struct silliconfs_device *device,
	struct vnode **root);

#endif
