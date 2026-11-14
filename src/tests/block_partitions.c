#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/block.h"
#include "kernel/errno.h"
static _Alignas(4096) unsigned char data[32768], buffer[4096];
static int read_disk(struct block_device *d, uint64_t s, uint32_t n, void *p)
{ memcpy(p,data+s*d->logical_sector_size,n*d->logical_sector_size); return 0; }
static int write_disk(struct block_device *d, uint64_t s, uint32_t n, const void *p)
{ memcpy(data+s*d->logical_sector_size,p,n*d->logical_sector_size); return 0; }
static int flush_disk(struct block_device *d) { (void)d; return 0; }
static const struct block_device_operations ops={read_disk,write_disk,flush_disk};
int main(void)
{
 struct block_device disk={.name="ada0",.logical_sector_size=512,.sector_count=64,.operations=&ops};
 struct block_device a={0},b={0},bad={0};
 assert(block_device_register(&disk)==0);
 assert(block_partition_register(&a,&disk,8,8,1)==0);
 assert(block_partition_register(&b,&disk,16,8,12)==0);
 assert(strcmp(a.name,"ada0p1")==0 && strcmp(b.name,"ada0p12")==0);
 memset(buffer,0x5a,512);
 assert(block_write(&a,7,1,buffer)==0);
 assert(data[15*512]==0x5a && data[16*512]==0);
 memset(buffer,0,sizeof(buffer));
 assert(block_read(&a,7,1,buffer)==0 && buffer[0]==0x5a);
 assert(block_read(&a,8,1,buffer)==-EINVAL);
 assert(block_write(&a,7,2,buffer)==-EINVAL);
 assert(block_partition_register(&bad,&disk,63,2,2)==-EINVAL);
 assert(block_partition_register(&bad,&a,0,1,2)==-EINVAL);
 assert(block_flush(&a)==0);
 a.mounted=true;
 assert(!block_raw_write_allowed(&a) && !block_raw_write_allowed(&disk));
 assert(block_raw_write_allowed(&b));
 a.mounted=false; disk.mounted=true;
 assert(!block_raw_write_allowed(&a) && !block_raw_write_allowed(&b));
 puts("block partition boundaries, parent offsets, flush and mounted hierarchy: PASS");
}
