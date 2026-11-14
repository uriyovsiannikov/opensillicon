#ifndef KERNEL_PROCFS_H
#define KERNEL_PROCFS_H

#include "kernel/vfs.h"

void procfs_set_cmdline(const char *cmdline);
int procfs_mount(const char *path);

#endif
