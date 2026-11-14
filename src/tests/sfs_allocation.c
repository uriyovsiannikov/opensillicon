#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fs/silliconfs/silliconfs.c"
static uint8_t maps[2][4096];
static unsigned reads,writes,fail_read,fail_write;
static uint32_t last_data;
static int read_test(void *context,uint32_t block,void *out)
{
 (void)context;assert(block==1 || block==2);reads++;
 if(reads==fail_read)return -SFS_EIO;
 memcpy(out,maps[block-1],4096);return 0;
}
static int write_test(void *context,uint32_t block,const void *data)
{
 (void)context;writes++;
 if(writes==fail_write)return -SFS_EIO;
 if(block==1 || block==2)memcpy(maps[block-1],data,4096);
 else {assert(zero_bytes(data,4096));last_data=block;}
 return 0;
}
int main(void)
{
 struct silliconfs fs={.device={.read_block=read_test,.write_block=write_test},
  .super={.data_start=100,.total_blocks=32771,.block_bitmap_start=1,.block_bitmap_blocks=2}};
 uint32_t block=0;
 memset(maps,255,sizeof(maps));
 /* Reserved prefix bits and beyond-end bits must never be allocated. */
 maps[0][0]=0;maps[1][0]=0xfc;
 assert(allocate_block(&fs,&block)==0 && block==32768 && last_data==block && reads==2);
 assert(allocate_block(&fs,&block)==0 && block==32769 && reads==4);
 maps[1][0]&=(uint8_t)~4U;
 assert(allocate_block(&fs,&block)==0 && block==32770 && reads==6);
 assert(allocate_block(&fs,&block)==-SFS_ENOSPC && reads==8);
 /* Data initialization failure returns its error and releases the reserved bit. */
 maps[0][2500]&=(uint8_t)~1U;reads=writes=0;fail_write=2;
 assert(allocate_block(&fs,&block)==-SFS_EIO && !(maps[0][2500]&1) && writes==3);
 reads=writes=0;fail_write=1;
 assert(allocate_block(&fs,&block)==-SFS_EIO && !(maps[0][2500]&1) && writes==1);
 reads=writes=0;fail_write=0;fail_read=1;
 assert(allocate_block(&fs,&block)==-SFS_EIO && writes==0);
 puts("SilliconFS allocation: bitmap-block I/O bound, range limits and write-failure rollback: PASS");
}
