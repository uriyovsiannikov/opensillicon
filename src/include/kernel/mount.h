#ifndef KERNEL_MOUNT_H
#define KERNEL_MOUNT_H
int filesystem_mount(const char *source, const char *target);
int filesystem_unmount(const char *target);
#endif
