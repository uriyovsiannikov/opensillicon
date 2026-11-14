#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/block.h"
#include "kernel/mount.h"
#include "kernel/vfs.h"
#include "kernel/fd.h"
#include "kernel/errno.h"
static size_t live,budget=SIZE_MAX;
static bool busy_directory,fail_flush;
void *kheap_alloc(size_t n) {if(!budget)return NULL;if(budget!=SIZE_MAX)budget--;void *p=malloc(n);if(p)live++;return p;}
bool kheap_free(void *p) {if(p){assert(live);live--;}free(p);return true;}
void klog(const char *s) {fputs(s,stderr);}
_Noreturn void kernel_halt(void) {abort();}
bool process_directory_busy(const char *p) {(void)p;return busy_directory;}
static int read_disk(struct block_device *d,uint64_t s,uint32_t n,void *p)
{FILE *f=d->context;return fseek(f,(long)(s*d->logical_sector_size),SEEK_SET)==0 && fread(p,d->logical_sector_size,n,f)==n ? 0 : -EIO;}
static int write_disk(struct block_device *d,uint64_t s,uint32_t n,const void *p)
{FILE *f=d->context;return fseek(f,(long)(s*d->logical_sector_size),SEEK_SET)==0 && fwrite(p,d->logical_sector_size,n,f)==n ? 0 : -EIO;}
static int flush_disk(struct block_device *d) {return fail_flush || fflush(d->context)!=0 ? -EIO : 0;}
static const struct block_device_operations ops={read_disk,write_disk,flush_disk};
static void directory(const char *path)
{struct vnode *v;assert(vfs_create_path(path,VNODE_DIRECTORY,0700,&v)==0);vnode_put(v);}
int main(int argc,char **argv)
{
 assert(argc==3);FILE *f=fopen(argv[1],"r+b");assert(f);
 assert(fseek(f,0,SEEK_END)==0);uint64_t bytes=(uint64_t)ftell(f);
 struct block_device disk={.name="ada0",.sector_count=bytes/512,.logical_sector_size=512,.operations=&ops,.context=f};
 FILE *second=fopen(argv[2],"r+b");assert(second);
 struct block_device disk2=disk;strcpy(disk2.name,"ada1");disk2.context=second;
 assert(block_device_register(&disk)==0 && block_device_register(&disk2)==0 && vfs_init());
 struct vnode *root=ramfs_create_root();assert(root);assert(vfs_mount("/",root)==0);vnode_put(root);
 directory("/mnt");
 assert(filesystem_mount("/dev/ada0","/mnt/")==0 && disk.mounted && disk.references==2);
 assert(!block_raw_write_allowed(&disk));
 struct fd_object *file;size_t n;
 assert(vfs_open("/mnt/probe",VFS_O_RDWR|VFS_O_CREAT,0640,&file)==0);
 assert(fd_object_write(file,"durable",7,&n)==0 && n==7);
 assert(file->operations->sync(file->context)==0);
 assert(filesystem_unmount("/mnt")==-EBUSY);fd_object_put(file);
 directory("/mnt/sub");assert(filesystem_mount("/dev/ada1","/mnt/sub")==0);
 assert(filesystem_unmount("/mnt")==-EBUSY);
 assert(filesystem_unmount("/mnt/sub")==0 && !disk2.mounted && disk2.references==1);
 busy_directory=true;assert(filesystem_unmount("/mnt")==-EBUSY);busy_directory=false;
 fail_flush=true;
 assert(filesystem_sync_all()==-EIO);
 assert(filesystem_unmount("/mnt")==-EIO && disk.mounted);
 fail_flush=false;
 assert(filesystem_unmount("/mnt/")==0 && !disk.mounted && disk.references==1);
 assert(filesystem_mount("/dev/ada0","/mnt")==0);
 assert(vfs_open("/mnt/probe",VFS_O_RDONLY,0,&file)==0);
 char data[8]={0};assert(fd_object_read(file,data,7,&n)==0 && n==7 && strcmp(data,"durable")==0);
 fd_object_put(file);assert(filesystem_unmount("/mnt")==0);
 size_t baseline=live;
 for(size_t i=0;i<24;i++) {
  budget=i;int result=filesystem_mount("/dev/ada0","/mnt");budget=SIZE_MAX;
  if(!result)assert(filesystem_unmount("/mnt")==0);
  else assert(result==-ENOMEM);
  assert(!disk.mounted && disk.references==1 && live==baseline);
 }
 assert(filesystem_unmount("/")==-EINVAL);
 assert(fseek(f,0,SEEK_SET)==0 && fputc('X',f)!=EOF && fflush(f)==0);
 assert(filesystem_mount("/dev/ada0","/mnt")==-EIO && !disk.mounted && disk.references==1 && live==baseline);
 fclose(f);fclose(second);puts("temporary SilliconFS mounts: open-file/CWD protection, fsync, flush failure and allocation rollback: PASS");
}
