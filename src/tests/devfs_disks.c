#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/block.h"
#include "kernel/devfs.h"
#include "kernel/fd.h"
#include "kernel/errno.h"
#include "kernel/vfs.h"

static bool fail_allocation;
void *kheap_alloc(size_t n) { return fail_allocation ? NULL : malloc(n); }
bool kheap_free(void *p) { free(p); return true; }
void klog(const char *s) { fputs(s, stderr); }
_Noreturn void kernel_halt(void) { abort(); }
bool tty_write(const void *p, size_t n) { (void)p; (void)n; return true; }
bool serial_write(const void *p, size_t n) { (void)p; (void)n; return true; }
int tty_read(void *p, size_t n, size_t *r) { (void)p; (void)n; *r=0; return 0; }
bool kernel_random_bytes(void *p, size_t n) { memset(p, 0, n); return true; }
void process_wake_channel(void *p) { (void)p; }

static unsigned char storage[16384];
static int disk_read(struct block_device *d, uint64_t lba, uint32_t n, void *p)
{ memcpy(p, storage + lba*d->logical_sector_size, n*d->logical_sector_size); return 0; }
static int disk_write(struct block_device *d, uint64_t lba, uint32_t n, const void *p)
{ memcpy(storage + lba*d->logical_sector_size, p, n*d->logical_sector_size); return 0; }
static int disk_flush(struct block_device *d) { (void)d; return 0; }
static const struct block_device_operations ops = { disk_read, disk_write, disk_flush };

int main(void)
{
 struct block_device disk = { .name="ada0", .logical_sector_size=512,
  .sector_count=32, .operations=&ops };
 struct block_device ram = { .name="ram0", .logical_sector_size=4096,
  .sector_count=4, .operations=&ops };
 struct vnode *root, *directory;
 struct fd_object *file;
 struct vfs_stat st;
 uint64_t position;
 size_t amount;
 unsigned char bytes[8193];
 assert(block_device_register(&disk)==0);
 assert(block_device_register(&ram)==0);
 assert(vfs_init());
 root=ramfs_create_root(); assert(root);
 assert(vfs_mount("/",root)==0); vnode_put(root);
 assert(vfs_create_path("/dev",VNODE_DIRECTORY,0755,&directory)==0);
 vnode_put(directory);
 assert(devfs_mount("/dev")==0);
 assert(vfs_stat_path("/dev/ada0",&st)==0);
 assert(st.size==sizeof(storage) && st.mode==0600 && st.uid==0 && st.type==VNODE_DEVICE);
 assert(vfs_open_as("/dev/ada0",VFS_O_RDWR,0,1000,1000,&file)==-EACCES);
 assert(vfs_open_as("/dev/ada0",VFS_O_RDONLY,0,1000,1000,&file)==-EACCES);
 assert(vfs_open("/dev/ada0",VFS_O_RDWR,0,&file)==0);
 memset(bytes+1,0xa5,1024);
 assert(fd_object_seek(file,512,SEEK_SET,&position)==0 && position==512);
 assert(fd_object_write(file,bytes+1,1024,&amount)==0 && amount==1024);
 assert(storage[511]==0 && storage[512]==0xa5 && storage[1535]==0xa5 && storage[1536]==0);
 assert(fd_object_seek(file,512,SEEK_SET,&position)==0);
 memset(bytes,0,sizeof(bytes));
 assert(fd_object_read(file,bytes+1,1024,&amount)==0 && amount==1024);
 assert(bytes[1]==0xa5 && bytes[1024]==0xa5);
 assert(fd_object_seek(file,1,SEEK_SET,&position)==0);
 assert(fd_object_read(file,bytes,512,&amount)==-EINVAL);
 assert(fd_object_seek(file,-512,SEEK_END,&position)==0);
 assert(fd_object_read(file,bytes,1024,&amount)==0 && amount==512);
 assert(fd_object_read(file,bytes,512,&amount)==0 && amount==0);
 assert(fd_object_write(file,bytes,512,&amount)==-ENOSPC);
 assert(fd_object_seek(file,-1,SEEK_SET,&position)==-EINVAL);
 assert(fd_object_seek(file,0,SEEK_SET,&position)==0);
 assert(fd_object_write(file,bytes,513,&amount)==-EINVAL);
 disk.mounted=true;
 assert(fd_object_write(file,bytes,512,&amount)==-EBUSY);
 assert(storage[512]==0xa5);
 disk.mounted=false;
 fail_allocation=true;
 assert(fd_object_read(file,bytes,512,&amount)==-ENOMEM);
 fail_allocation=false;
 fd_object_put(file);
 assert(vfs_open("/dev/ram0",VFS_O_RDWR,0,&file)==0);
 assert(fd_object_write(file,bytes,512,&amount)==-EINVAL);
 assert(fd_object_write(file,bytes+1,4096,&amount)==0 && amount==4096);
 fd_object_put(file);
 assert(vfs_open("/dev/zero",VFS_O_RDONLY,0,&file)==0);
 assert(fd_object_seek(file,0,SEEK_SET,&position)==-ESPIPE);
 fd_object_put(file);
 puts("devfs disks: permissions, aligned I/O, seeking, boundaries, mounted protection: PASS");
 return 0;
}
