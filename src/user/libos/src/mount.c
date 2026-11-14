#include <errno.h>
#include <string.h>
#include <sys/mount.h>
#include "abi/syscall.h"
#include "internal.h"
int mount(const char *type,const char *directory,int flags,void *data)
{
 if(!type || strcmp(type,"silliconfs") || !data) {errno=EINVAL;return -1;}
 struct silliconfs_args *args=data;
 return (int)__libos_call(SYSCALL_MOUNT,(long)args->fspec,(long)directory,flags,0,0,0);
}
int unmount(const char *directory,int flags)
{ return (int)__libos_call(SYSCALL_UNMOUNT,(long)directory,flags,0,0,0,0); }
int umount(const char *directory) {return unmount(directory,0);}
int fsync(int fd) {return (int)__libos_call(SYSCALL_FSYNC,fd,0,0,0,0,0);}
int sync_all(void) {return (int)__libos_call(SYSCALL_SYNC,0,0,0,0,0,0);}
void sync(void) {(void)sync_all();}
