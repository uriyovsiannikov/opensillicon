#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"
#define MIB (1024U*1024U)
#define TABLE_BYTES 16384U
int gpt_layout_plan(const struct storage_io *io,uint32_t esp_mib,struct gpt_layout *p)
{
 if(!io || !p || (io->sector!=512 && io->sector!=4096) ||
    io->bytes%io->sector || io->bytes>INT64_MAX || esp_mib<64) return -EINVAL;
 uint64_t sectors=io->bytes/io->sector, table=TABLE_BYTES/io->sector;
 uint64_t align=MIB/io->sector, esp=(uint64_t)esp_mib*MIB/io->sector;
 if(sectors<2*table+4) return -ENOSPC;
 uint64_t first=((2+table+align-1)/align)*align, last=sectors-table-2;
 if(first>last || esp>last-first) return -ENOSPC;
 uint64_t root=first+esp, fs_align=4096/io->sector;
 uint64_t end=((last+1)/fs_align)*fs_align;
 if(end<=root || end-root<(64U*MIB)/io->sector ||
    (end-root)*io->sector/4096>UINT32_MAX) return -ENOSPC;
 *p=(struct gpt_layout){.sectors=sectors,.table_sectors=table,.first_usable=2+table,
  .last_usable=last,.esp_first=first,.esp_sectors=esp,.root_first=root,.root_sectors=end-root};
 return 0;
}
static void header(uint8_t *p,const struct storage_io *io,const struct gpt_layout *l,
 const uint8_t *uuid,uint32_t array_crc,int backup)
{
 memset(p,0,io->sector); memcpy(p,"EFI PART",8);
 storage_le32(p+8,0x10000); storage_le32(p+12,92);
 storage_le64(p+24,backup ? l->sectors-1 : 1);
 storage_le64(p+32,backup ? 1 : l->sectors-1);
 storage_le64(p+40,l->first_usable); storage_le64(p+48,l->last_usable);
 memcpy(p+56,uuid,16);
 storage_le64(p+72,backup ? l->sectors-l->table_sectors-1 : 2);
 storage_le32(p+80,128); storage_le32(p+84,128); storage_le32(p+88,array_crc);
 storage_le32(p+16,storage_crc32(p,92));
}
static void entry(uint8_t *p,const uint8_t *type,const uint8_t *uuid,
 uint64_t first,uint64_t count,const char *label)
{
 memcpy(p,type,16); memcpy(p+16,uuid,16);
 storage_le64(p+32,first); storage_le64(p+40,first+count-1);
 for(unsigned i=0;label[i] && i<36;i++) storage_le16(p+56+i*2,(uint16_t)(uint8_t)label[i]);
}
static int verify(const struct storage_io *io,uint64_t offset,const uint8_t *expected,size_t bytes)
{
 uint8_t block[4096];
 while(bytes) {
  size_t n=bytes>sizeof(block) ? sizeof(block) : bytes;
  int error=io->read(io->context,offset,block,n);
  if(error) return error;
  if(memcmp(expected,block,n)!=0) return -EIO;
  expected+=n; offset+=n; bytes-=n;
 }
 return 0;
}
int gpt_layout_write(const struct storage_io *io,uint32_t esp_mib,const uint8_t uuids[48])
{
 static const uint8_t esp_type[16]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0,0xa0,0xc9,0x3e,0xc9,0x3b};
 static const uint8_t sfs_type[16]={0x46,0x4c,0x49,0x53,0,0x53,0,0x40,0x80,0,0x4f,0x50,0x45,0x4e,0x53,0x49};
 static const uint8_t zero[16];
 struct gpt_layout layout;
 int error=gpt_layout_plan(io,esp_mib,&layout);
 if(error) return error;
 if(!uuids || !io->read || !io->write || !io->flush) return -EINVAL;
 for(unsigned i=0;i<3;i++) {
  if(memcmp(uuids+i*16,zero,16)==0) return -EINVAL;
  for(unsigned j=0;j<i;j++) if(memcmp(uuids+i*16,uuids+j*16,16)==0) return -EINVAL;
 }
 uint8_t *array=calloc(1,TABLE_BYTES); if(!array) return -ENOMEM;
 entry(array,esp_type,uuids+16,layout.esp_first,layout.esp_sectors,"EFI System");
 entry(array+128,sfs_type,uuids+32,layout.root_first,layout.root_sectors,"OpenSillicon");
 uint32_t crc=storage_crc32(array,TABLE_BYTES);
 uint8_t block[4096];
 uint64_t backup_array=(layout.sectors-layout.table_sectors-1)*io->sector;
 header(block,io,&layout,uuids,crc,1);
 error=io->write(io->context,backup_array,array,TABLE_BYTES);
 if(!error) error=io->write(io->context,(layout.sectors-1)*io->sector,block,io->sector);
 if(!error) error=io->flush(io->context);
 if(error) goto out;
 header(block,io,&layout,uuids,crc,0);
 error=io->write(io->context,2*io->sector,array,TABLE_BYTES);
 if(!error) error=io->write(io->context,io->sector,block,io->sector);
 if(!error) error=io->flush(io->context);
 if(error) goto out;
 error=verify(io,io->sector,block,io->sector);
 if(!error) error=verify(io,2*io->sector,array,TABLE_BYTES);
 if(!error) error=verify(io,backup_array,array,TABLE_BYTES);
 if(error) goto out;
 header(block,io,&layout,uuids,crc,1);
 error=verify(io,(layout.sectors-1)*io->sector,block,io->sector);
 if(error) goto out;
 /* Publish the protective MBR only after both GPT copies are durable. */
 memset(block,0,io->sector);
 block[447]=0; block[448]=2; block[449]=0; block[450]=0xee;
 block[451]=0xff; block[452]=0xff; block[453]=0xff;
 storage_le32(block+454,1);
 storage_le32(block+458,layout.sectors-1>UINT32_MAX ? UINT32_MAX : (uint32_t)(layout.sectors-1));
 block[510]=0x55; block[511]=0xaa;
 error=io->write(io->context,0,block,io->sector);
 if(!error) error=io->flush(io->context);
 if(!error) error=verify(io,0,block,io->sector);
out:
 free(array); return error;
}
