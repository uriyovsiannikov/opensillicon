#ifndef LIBOS_SYS_MOUNT_H
#define LIBOS_SYS_MOUNT_H
struct silliconfs_args {
	const char *fspec;
};
int mount(const char *type, const char *directory, int flags, void *data);
int unmount(const char *directory, int flags);
int umount(const char *directory);
#endif
