#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kernel/devfs.h"
#include "kernel/block.h"
#include "kernel/gpt.h"
#include "abi/disk.h"
#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/random.h"
#include "kernel/tty.h"
#include "kernel/hda_audio.h"

enum devfs_device {
	DEVFS_DIRECTORY,
	DEVFS_CONSOLE,
	DEVFS_TTY,
	DEVFS_NULL,
	DEVFS_ZERO,
	DEVFS_RANDOM,
	DEVFS_URANDOM,
	DEVFS_DISK,
	DEVFS_AUDIO
};

struct devfs_node {
	struct vnode vnode;
	struct devfs_node *children;
	struct devfs_node *next;
	enum devfs_device device;
	struct block_device *block;
	char name[VFS_NAME_MAX + 1];
};

static struct devfs_node *mounted_root;

static int devfs_lookup(struct vnode *, const char *, size_t, struct vnode **);
static int devfs_read(struct vnode *, uint64_t, void *, size_t, size_t *);
static int devfs_write(struct vnode *, uint64_t, const void *, size_t, size_t *);
static int devfs_readdir(struct vnode *, uint64_t, struct vfs_dirent *);
static void devfs_destroy(struct vnode *);
static int devfs_disk_ioctl(struct vnode *, uint64_t, void *);

static const struct vnode_operations devfs_operations = {
	.lookup = devfs_lookup,
	.read = devfs_read,
	.write = devfs_write,
	.readdir = devfs_readdir,
	.destroy = devfs_destroy
};

static const struct vnode_operations devfs_audio_operations = {
    .open_device = hda_audio_open,
    .destroy = devfs_destroy,
};

static const struct vnode_operations devfs_disk_operations = {
    .ioctl = devfs_disk_ioctl,
    .seekable = true,
    .read = devfs_read,
    .write = devfs_write,
    .destroy = devfs_destroy,
};

static int
devfs_disk_ioctl(struct vnode *vnode, uint64_t request, void *argument)
{
	struct devfs_node *node = vnode->private_data;
	struct block_device *disk = node->block;
	if (!disk->registered)
		return -ENODEV;
	if (request == OS_DIOCREREADGPT) {
		int error = gpt_rescan(disk);
		return error ? error : devfs_refresh_disks();
	}
	if (request == OS_DIOCGFLUSH)
		return block_flush(disk);
	if (argument == NULL)
		return -EINVAL;
	if (request == OS_DIOCGMODEL || request == OS_DIOCGIDENT) {
		struct block_device *whole = disk->parent ? disk->parent : disk;
		const char *text =
		    request == OS_DIOCGMODEL ? whole->model : whole->ident;
		size_t size = request == OS_DIOCGMODEL ? 41 : 256;
		memset(argument, 0, size);
		if (!text[0])
			text = whole->name;
		memcpy(argument, text, strlen(text));
		return 0;
	}
	if (request == OS_DIOCGPARTINFO) {
		struct os_disk_partinfo *info = argument;
		memset(info, 0, sizeof(*info));
		info->media_bytes =
		    disk->sector_count * disk->logical_sector_size;
		info->first_sector = disk->first_sector;
		info->partition_number = disk->partition_number;
		if (disk->parent) {
			info->flags |= OS_DISK_PARTITION;
			memcpy(info->parent, disk->parent->name,
			    strlen(disk->parent->name) + 1);
		}
		if (disk->mounted)
			info->flags |= OS_DISK_MOUNTED;
		if (block_raw_write_allowed(disk))
			info->flags |= OS_DISK_RAW_WRITE_ALLOWED;
		memcpy(info->uuid, disk->partition_uuid, 16);
		memcpy(info->type_uuid, disk->partition_type, 16);
		return 0;
	}
	if (request == OS_DIOCGSECTORSIZE) {
		*(uint32_t *)argument = disk->logical_sector_size;
		return 0;
	}
	if (request == OS_DIOCGMEDIASIZE) {
		uint64_t bytes = disk->sector_count * disk->logical_sector_size;
		if (bytes > INT64_MAX)
			return -EOVERFLOW;
		*(int64_t *)argument = (int64_t)bytes;
		return 0;
	}
	return -ENOTTY;
}

/* Raw disk I/O follows sector boundaries; callers need not align buffers.
 * Never perform a read/modify/write of neighbouring bytes on their behalf. */
static int
devfs_disk_io(struct devfs_node *node, uint64_t offset, void *buffer,
	size_t length, size_t *transferred, bool write)
{
	struct block_device *device = node->block;
	if (!device->registered)
		return -ENODEV;
	uint64_t size = device->sector_count * device->logical_sector_size;
	size_t sector = device->logical_sector_size;
	*transferred = 0;
	if (length == 0) return 0;
	if (offset % sector != 0 || length % sector != 0) return -EINVAL;
	if (write && !block_raw_write_allowed(device))
		return -EBUSY;
	if (offset >= size) return write ? -ENOSPC : 0;
	if (length > size - offset) length = (size_t)(size - offset);
	/* block_read/write require sector-aligned buffers. */
	void *allocation = kheap_alloc(4096U + 4095U);
	if (allocation == NULL) return -ENOMEM;
	void *bounce = (void *)(((uintptr_t)allocation + 4095U) & ~(uintptr_t)4095U);
	int error = 0;
	while (*transferred < length) {
		uint8_t *bytes = (uint8_t *)buffer + *transferred;
		if (write) memcpy(bounce, bytes, sector);
		error = write ? block_write(device, offset / sector, 1, bounce) :
		    block_read(device, offset / sector, 1, bounce);
		if (error != 0) break;
		if (!write) memcpy(bytes, bounce, sector);
		*transferred += sector;
		offset += sector;
	}
	(void)kheap_free(allocation);
	/* Report completed sectors as a short transfer after a later failure. */
	return *transferred != 0 ? 0 : error;
}

static struct devfs_node *
devfs_from_vnode(struct vnode *vnode)
{
	return (struct devfs_node *)vnode->private_data;
}

static struct devfs_node *
devfs_allocate(const char *name, enum devfs_device device)
{
	struct devfs_node *node;
	uint64_t inode;
	enum vnode_type type = device == DEVFS_DIRECTORY ? VNODE_DIRECTORY :
	    VNODE_DEVICE;

	inode = vfs_allocate_inode();
	if (inode == 0)
		return NULL;
	node = kheap_alloc(sizeof(*node));
	if (node == NULL)
		return NULL;
	memset(node, 0, sizeof(*node));
	memcpy(node->name, name, strlen(name) + 1U);
	node->device = device;
	node->vnode = (struct vnode){
		.operations = &devfs_operations,
		.type = type,
		.mode = device == DEVFS_DIRECTORY ? 0555U :
		    (device == DEVFS_CONSOLE || device == DEVFS_TTY ? 0600U : 0666U),
		.inode = inode,
		.references = 1,
		.private_data = node
	};
    if (device == DEVFS_AUDIO) {
        node->vnode.operations = &devfs_audio_operations;
        node->vnode.mode = 0660;
        node->vnode.gid = 1000;
    }
	return node;
}

static void
devfs_destroy(struct vnode *vnode)
{
	struct devfs_node *node = devfs_from_vnode(vnode);
	struct devfs_node *child = node->children;

	while (child != NULL) {
		struct devfs_node *next = child->next;

		child->next = NULL;
		vnode_put(&child->vnode);
		child = next;
	}
	if (node->block)
		block_device_put(node->block);
	if (node == mounted_root)
		mounted_root = NULL;
	(void)kheap_free(node);
}

static int
devfs_lookup(struct vnode *directory, const char *name, size_t name_length,
	struct vnode **result)
{
	struct devfs_node *parent;
	struct devfs_node *child;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -ENOTDIR;
	if (name == NULL || name_length == 0 || name_length > VFS_NAME_MAX ||
	    result == NULL)
		return -EINVAL;
	*result = NULL;
	parent = devfs_from_vnode(directory);
	for (child = parent->children; child != NULL; child = child->next) {
		if (strlen(child->name) == name_length &&
		    memcmp(child->name, name, name_length) == 0) {
			if (!vnode_get(&child->vnode))
				return -EINVAL;
			*result = &child->vnode;
			return 0;
		}
	}
	return -ENOENT;
}

static int
devfs_read(struct vnode *vnode, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct devfs_node *node;

	(void)offset;
	if (vnode == NULL || read_length == NULL ||
	    (buffer == NULL && length != 0))
		return -EINVAL;
	*read_length = 0;
	node = devfs_from_vnode(vnode);
	switch (node->device) {
	case DEVFS_CONSOLE:
	case DEVFS_TTY:
		return tty_read(buffer, length, read_length);
	case DEVFS_NULL:
		return 0;
	case DEVFS_ZERO:
		if (length != 0)
			memset(buffer, 0, length);
		*read_length = length;
		return 0;
	case DEVFS_RANDOM:
	case DEVFS_URANDOM:
		if (length > KERNEL_RANDOM_MAX_REQUEST)
			length = KERNEL_RANDOM_MAX_REQUEST;
		if (length == 0)
			return 0;
		if (!kernel_random_bytes(buffer, length))
			return -11;
		*read_length = length;
		return 0;
	case DEVFS_DISK:
		return devfs_disk_io(node, offset, buffer, length, read_length, false);
	case DEVFS_AUDIO:
	case DEVFS_DIRECTORY:
		return -EINVAL;
	}
	return -EINVAL;
}

static int
devfs_write(struct vnode *vnode, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	struct devfs_node *node;

	(void)offset;
	if (vnode == NULL || write_length == NULL ||
	    (buffer == NULL && length != 0))
		return -EINVAL;
	*write_length = 0;
	node = devfs_from_vnode(vnode);
	switch (node->device) {
	case DEVFS_CONSOLE:
	case DEVFS_TTY:
		if (!tty_write(buffer, length) || !serial_write(buffer, length))
			return -EIO;
		*write_length = length;
		return 0;
	case DEVFS_NULL:
	case DEVFS_ZERO:
		*write_length = length;
		return 0;
	case DEVFS_RANDOM:
	case DEVFS_URANDOM:
		return -EINVAL;
	case DEVFS_DISK:
		return devfs_disk_io(node, offset, (void *)buffer, length, write_length, true);
	case DEVFS_AUDIO:
	case DEVFS_DIRECTORY:
		return -EINVAL;
	}
	return -EINVAL;
}

static int
devfs_readdir(struct vnode *directory, uint64_t index,
	struct vfs_dirent *entry)
{
	struct devfs_node *child;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -ENOTDIR;
	if (entry == NULL)
		return -EINVAL;
	child = devfs_from_vnode(directory)->children;
	while (child != NULL && index != 0) {
		child = child->next;
		index--;
	}
	if (child == NULL)
		return 0;
	memset(entry, 0, sizeof(*entry));
	entry->inode = child->vnode.inode;
	entry->mode = child->vnode.mode;
	entry->type = (uint32_t)child->vnode.type;
	memcpy(entry->name, child->name, strlen(child->name) + 1U);
	return 1;
}

static struct devfs_node *
disk_node(struct block_device *block)
{
	struct devfs_node *node = devfs_allocate(block->name, DEVFS_DISK);
	if (!node)
		return NULL;
	if (!block_device_get(block)) {
		vnode_put(&node->vnode);
		return NULL;
	}
	node->block = block;
	node->vnode.operations = &devfs_disk_operations;
	node->vnode.mode = 0600;
	node->vnode.size = block->sector_count * block->logical_sector_size;
	return node;
}

int
devfs_refresh_disks(void)
{
	if (!mounted_root)
		return 0;
	struct devfs_node *list = NULL, **tail = &list;
	for (size_t i = 0; i < block_device_count(); i++) {
		struct devfs_node *node = disk_node(block_device_at(i));
		if (!node) {
			while (list) {
				struct devfs_node *next = list->next;
				list->next = NULL;
				vnode_put(&list->vnode);
				list = next;
			}
			return -ENOMEM;
		}
		*tail = node;
		tail = &node->next;
	}
	struct devfs_node **link = &mounted_root->children;
	while (*link && (*link)->device != DEVFS_DISK)
		link = &(*link)->next;
	struct devfs_node *old = *link;
	*link = list;
	while (old) {
		struct devfs_node *next = old->next;
		old->next = NULL;
		vnode_put(&old->vnode);
		old = next;
	}
	return 0;
}

static struct vnode *
devfs_create_root(void)
{
	static const struct {
		const char *name;
		enum devfs_device device;
	} devices[] = {
		{ "console", DEVFS_CONSOLE },
		{ "tty", DEVFS_TTY },
		{ "null", DEVFS_NULL },
		{ "zero", DEVFS_ZERO },
		{ "random", DEVFS_RANDOM },
		{ "urandom", DEVFS_URANDOM },
		{ "audio", DEVFS_AUDIO }
	};
	struct devfs_node *root;
	struct devfs_node **tail;
	size_t i;

	root = devfs_allocate("", DEVFS_DIRECTORY);
	if (root == NULL)
		return NULL;
	tail = &root->children;
	for (i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
		struct devfs_node *child = devfs_allocate(devices[i].name,
		    devices[i].device);

		if (child == NULL) {
			vnode_put(&root->vnode);
			return NULL;
		}
		*tail = child;
		tail = &child->next;
	}
	for (i = 0; i < block_device_count(); i++) {
		struct block_device *block = block_device_at(i);
		struct devfs_node *child = disk_node(block);
		if (child == NULL) {
			vnode_put(&root->vnode);
			return NULL;
		}
		*tail = child;
		tail = &child->next;
	}
	return &root->vnode;
}

int
devfs_mount(const char *path)
{
	if (mounted_root)
		return -EBUSY;
	struct vnode *root = devfs_create_root();
	int error;

	if (root == NULL)
		return -ENOMEM;
	error = vfs_mount(path, root);
	if (error == 0)
		mounted_root = devfs_from_vnode(root);
	vnode_put(root);
	return error;
}
