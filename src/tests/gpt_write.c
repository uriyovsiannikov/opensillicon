#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../user/storage/storage.h"
#include "kernel/gpt.h"
static FILE *image;
static unsigned writes, flushes, reads, fail_write, fail_flush, corrupt_read;
void *kheap_alloc(size_t n) { return malloc(n); }
bool kheap_free(void *p) { free(p); return true; }
void klog(const char *p) { (void)p; }
static int read_image(void *c,uint64_t offset,void *p,size_t n)
{
 (void)c; ++reads;
 if(fseek(image,(long)offset,SEEK_SET)!=0 || fread(p,1,n,image)!=n) return -EIO;
 if(reads==corrupt_read) ((unsigned char *)p)[0]^=1;
 return 0;
}
static int write_image(void *c,uint64_t offset,const void *p,size_t n)
{
 (void)c;
 if(++writes==fail_write) return -EIO;
 if(fseek(image,(long)offset,SEEK_SET)!=0 || fwrite(p,1,n,image)!=n) return -EIO;
 return 0;
}
static int flush_image(void *c)
{ (void)c; if(++flushes==fail_flush) return -EIO; return fflush(image)==0 ? 0 : -EIO; }
static int block_reader(struct block_device *d,uint64_t s,uint32_t n,void *p)
{ return read_image(NULL,s*d->logical_sector_size,p,n*d->logical_sector_size); }
static int block_writer(struct block_device *d,uint64_t s,uint32_t n,const void *p)
{ (void)d;(void)s;(void)n;(void)p;abort(); }
static const struct block_device_operations ops={.read=block_reader,.write=block_writer};
int main(int argc,char **argv)
{
 assert(argc==6);
 image=fopen(argv[1],"r+b"); assert(image);
 uint32_t sector=(uint32_t)atoi(argv[2]);
 fail_write=(unsigned)atoi(argv[3]);fail_flush=(unsigned)atoi(argv[4]);corrupt_read=(unsigned)atoi(argv[5]);
 struct storage_io io={.bytes=192U*1024U*1024U,.sector=sector,.read=read_image,.write=write_image,.flush=flush_image};
 struct gpt_layout plan;
 assert(gpt_layout_plan(&io,64,&plan)==0 && writes==0);
 assert(plan.esp_first*sector==1024U*1024U && plan.root_first*sector==65U*1024U*1024U);
 assert((plan.root_sectors*sector)%4096==0);
 struct storage_io invalid=io;invalid.bytes=128U*1024U*1024U;
 assert(gpt_layout_plan(&invalid,64,&plan)==-ENOSPC);
 assert(gpt_layout_plan(&io,63,&plan)==-EINVAL);
 unsigned char ids[48]={0};ids[0]=1;ids[16]=2;ids[32]=3;
 int result=gpt_layout_write(&io,64,ids);
 assert(result==((fail_write||fail_flush||corrupt_read) ? -EIO : 0));
 if(result==0) {
  struct block_device disk={.name="ada0",.logical_sector_size=sector,.sector_count=io.bytes/sector,.operations=&ops};
  assert(block_device_register(&disk)==0);
  struct gpt_table *gpt=malloc(sizeof(*gpt));assert(gpt);
  assert(gpt_read(&disk,gpt)==2 && !gpt->degraded);
  assert(gpt->partitions[0].number==1 && gpt->partitions[1].number==2);
  assert(gpt->partitions[1].first*sector==65U*1024U*1024U);
  assert(memcmp(gpt->partitions[1].uuid,ids+32,16)==0);
  free(gpt);
 }
 fclose(image);
}
