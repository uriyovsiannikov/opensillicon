#include <stdint.h>
#include <string.h>
#include "kernel/mount.h"
#include "kernel/block_silliconfs.h"
#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/process.h"
#define MOUNT_LIMIT 4
static struct managed_mount {
	void *allocation;
	struct block_silliconfs_adapter *adapter;
	struct block_device *block;
	char target[VFS_PATH_MAX];
} mounts[MOUNT_LIMIT];

int
filesystem_mount(const char *source, const char *target)
{
	char canonical[VFS_PATH_MAX];
	if (!source || strncmp(source, "/dev/", 5) || !source[5] ||
	    strchr(source + 5, '/'))
		return -EINVAL;
	int error = vfs_resolve_path("/", target, canonical, sizeof(canonical));
	if (error)
		return error;
	size_t length = strlen(canonical);
	if (length > 1 && canonical[length - 1] == '/')
		canonical[length - 1] = 0;
	struct block_device *block = NULL;
	for (size_t i = 0; i < block_device_count(); i++)
		if (strcmp(source + 5, block_device_at(i)->name) == 0)
			block = block_device_at(i);
	if (!block)
		return -ENODEV;
	if (!block_raw_write_allowed(block))
		return -EBUSY;
	struct managed_mount *slot = NULL;
	for (unsigned i = 0; i < MOUNT_LIMIT; i++)
		if (!mounts[i].block) {
			slot = &mounts[i];
			break;
		}
	if (!slot)
		return -ENOSPC;
	void *allocation =
	    kheap_alloc(sizeof(struct block_silliconfs_adapter) + 4095);
	if (!allocation)
		return -ENOMEM;
	struct block_silliconfs_adapter *adapter =
	    (void *)(((uintptr_t)allocation + 4095) & ~(uintptr_t)4095);
	if (!block_device_get(block)) {
		kheap_free(allocation);
		return -EOVERFLOW;
	}
	block->mounted = true;
	struct vnode *root = NULL;
	if (!block_silliconfs_init(adapter, block))
		error = -EINVAL;
	else
		error = silliconfs_mount(&adapter->filesystem, &root);
	if (!error)
		error = vfs_mount(canonical, root);
	vnode_put(root);
	if (error) {
		block->mounted = false;
		block_device_put(block);
		kheap_free(allocation);
		return error;
	}
	*slot = (struct managed_mount){
	    .allocation = allocation, .adapter = adapter, .block = block};
	memcpy(slot->target, canonical, strlen(canonical) + 1);
	return 0;
}

int
filesystem_unmount(const char *target)
{
	char canonical[VFS_PATH_MAX];
	int error = vfs_resolve_path("/", target, canonical, sizeof(canonical));
	if (error)
		return error;
	size_t length = strlen(canonical);
	if (length > 1 && canonical[length - 1] == '/')
		canonical[length - 1] = 0;
	struct managed_mount *slot = NULL;
	for (unsigned i = 0; i < MOUNT_LIMIT; i++)
		if (mounts[i].block && strcmp(mounts[i].target, canonical) == 0)
			slot = &mounts[i];
	if (!slot)
		return -EINVAL;
	if (process_directory_busy(canonical))
		return -EBUSY;
	error = vfs_unmount_check(canonical);
	if (!error)
		error = block_flush(slot->block);
	if (!error)
		error = vfs_unmount(canonical);
	if (error)
		return error;
	slot->block->mounted = false;
	block_device_put(slot->block);
	kheap_free(slot->allocation);
	memset(slot, 0, sizeof(*slot));
	return 0;
}

int
filesystem_sync_all(void)
{
	int error = 0;
	for (size_t i = 0; i < block_device_count(); i++) {
		struct block_device *block = block_device_at(i);
		if (block->mounted) {
			int result = block_flush(block);
			if (result && !error)
				error = result;
		}
	}
	return error;
}
