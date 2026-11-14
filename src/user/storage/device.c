#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <unistd.h>
#include "storage.h"

static int transfer(void *context,uint64_t offset,void *buffer,size_t size,int writing)
{
 struct storage_device *device=context;
 if(offset>device->io.bytes || size>device->io.bytes-offset) return -EINVAL;
 if(offset%device->io.sector || size%device->io.sector) return -EINVAL;
 off_t position=lseek(device->fd,(off_t)offset,SEEK_SET);
 if(position!=(off_t)offset) return position<0 ? -errno : -EIO;
 size_t done=0;
 while(done<size) {
  size_t chunk=size-done; if(chunk>4096) chunk=4096;
  ssize_t n=writing ? write(device->fd,(uint8_t *)buffer+done,chunk) :
   read(device->fd,(uint8_t *)buffer+done,chunk);
  if(n<0) return -errno;
  if(n==0 || (size_t)n>chunk || (size_t)n%device->io.sector) return -EIO;
  done+=(size_t)n;
 }
 return 0;
}
static int device_read(void *c,uint64_t o,void *p,size_t n)
{ return transfer(c,o,p,n,0); }
static int device_write(void *c,uint64_t o,const void *p,size_t n)
{ return transfer(c,o,(void *)p,n,1); }
static int device_flush(void *c)
{ return ioctl(((struct storage_device *)c)->fd,DIOCGFLUSH)==0 ? 0 : -errno; }
int storage_open(struct storage_device *device,const char *path,int writable)
{
 struct stat st; uint32_t sector=0; int64_t bytes=0;
 device->fd=open(path,writable ? O_RDWR : O_RDONLY);
 if(device->fd<0) return -errno;
 int error=0;
 if(fstat(device->fd,&st)!=0) error=-errno;
 else if(!S_ISCHR(st.st_mode)) error=-ENOTTY;
 else if(ioctl(device->fd,DIOCGSECTORSIZE,&sector)!=0 ||
         ioctl(device->fd,DIOCGMEDIASIZE,&bytes)!=0) error=-errno;
 else if((sector!=512 && sector!=4096) || bytes<=0 || (uint64_t)bytes%sector) error=-EINVAL;
 if(error) { close(device->fd); device->fd=-1; return error; }
 device->io=(struct storage_io){.context=device,.bytes=(uint64_t)bytes,.sector=sector,
  .read=device_read,.write=device_write,.flush=device_flush};
 return 0;
}
int storage_close(struct storage_device *device)
{ int result=close(device->fd); device->fd=-1; return result==0 ? 0 : -errno; }
