#ifndef KERNEL_DEVFS_H
#define KERNEL_DEVFS_H

#include "kernel/vfs.h"

int devfs_refresh_disks(void);
int devfs_mount(const char *path);

#endif
