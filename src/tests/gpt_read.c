#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/gpt.h"
#include "kernel/errno.h"
void *kheap_alloc(size_t n) { return malloc(n); }
bool kheap_free(void *p) { free(p); return true; }
void klog(const char *p) { (void)p; }
static int read_disk(struct block_device *d,uint64_t s,uint32_t n,void *p)
{
 FILE *f=d->context;
 if(fseek(f,(long)(s*d->logical_sector_size),SEEK_SET)!=0) return -EIO;
 return fread(p,d->logical_sector_size,n,f)==n ? 0 : -EIO;
}
static int write_disk(struct block_device *d,uint64_t s,uint32_t n,const void *p)
{ (void)d;(void)s;(void)n;(void)p; abort(); }
static int flush_disk(struct block_device *d) { (void)d; abort(); }
static const struct block_device_operations ops={read_disk,write_disk,flush_disk};
int main(int argc,char **argv)
{
 assert(argc==5);
 FILE *f=fopen(argv[1],"rb"); assert(f);
 struct block_device disk={.name="ada0",.logical_sector_size=(uint32_t)atoi(argv[2]),
  .sector_count=4096,.operations=&ops,.context=f};
 assert(block_device_register(&disk)==0);
 struct gpt_table *table=malloc(sizeof(*table)); assert(table);
 int result=gpt_read(&disk,table), expected=atoi(argv[3]);
 assert(expected<0 ? result<0 : result==expected);
 if(result>0) {
  assert(table->degraded==(atoi(argv[4])!=0));
  assert(table->backup==(atoi(argv[4])==2));
  assert(table->partitions[0].number==1 && table->partitions[1].number==3);
  assert(table->partitions[0].first==128 && table->partitions[0].last==255);
  gpt_discover(); assert(block_device_count()==3);
  assert(strcmp(block_device_at(2)->name,"ada0p3")==0);
  gpt_discover(); assert(block_device_count()==3);
  static _Alignas(4096) unsigned char data[4096];
  assert(block_read(block_device_at(1),128,1,data)==-EINVAL);
 } else { gpt_discover(); assert(block_device_count()==1); }
 free(table); fclose(f);
}
