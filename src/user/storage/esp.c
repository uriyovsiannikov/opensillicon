#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"
#define RESERVED 32U
#define EOC 0x0fffffffU
int fat32_plan(const struct storage_io *io,struct fat32_layout *p)
{
 if(!io || !p || (io->sector!=512 && io->sector!=4096) ||
    !io->bytes || io->bytes%io->sector || io->bytes/io->sector>UINT32_MAX) return -EINVAL;
 uint64_t sectors=io->bytes/io->sector;
 if(sectors<=RESERVED) return -ENOSPC;
 /* One sector per cluster. Overestimate FAT size, then derive usable clusters. */
 uint64_t fat=((sectors-RESERVED+2)*4+io->sector-1)/io->sector;
 uint64_t start=RESERVED+2*fat;
 if(start>=sectors || sectors-start<65525 || sectors-start>=0x0ffffff5U) return -ENOSPC;
 *p=(struct fat32_layout){.sectors=(uint32_t)sectors,.fat_sectors=(uint32_t)fat,
  .data_start=(uint32_t)start,.clusters=(uint32_t)(sectors-start)};
 return 0;
}
static void dirent(uint8_t *p,const char name[11],uint8_t attr,uint32_t cluster,uint32_t size)
{
 memcpy(p,name,11); p[11]=attr;
 storage_le16(p+16,0x21); storage_le16(p+18,0x21); storage_le16(p+24,0x21);
 storage_le16(p+20,(uint16_t)(cluster>>16)); storage_le16(p+26,(uint16_t)cluster);
 storage_le32(p+28,size);
}
static void long_name(uint8_t *p,const char *name,const char alias[11])
{
 static const unsigned offsets[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
 uint8_t checksum=0;
 for(unsigned i=0;i<11;i++) checksum=(uint8_t)(((checksum&1)<<7)+(checksum>>1)+(uint8_t)alias[i]);
 p[0]=0x41;p[11]=0x0f;p[13]=checksum;
 size_t length=strlen(name);
 for(unsigned i=0;i<13;i++) storage_le16(p+offsets[i],i<length ? (uint8_t)name[i] : i==length ? 0 : 0xffff);
}
static int write_at(const struct storage_io *io,uint64_t offset,const void *p,size_t n)
{ return io->write(io->context,offset,p,n); }
static int check_at(const struct storage_io *io,uint64_t offset,const uint8_t *p,size_t n)
{
 uint8_t buffer[4096];
 while(n) {
  size_t size=n>sizeof(buffer) ? sizeof(buffer) : n;
  int error=io->read(io->context,offset,buffer,size);
  if(error) return error;
  if(memcmp(buffer,p,size)) return -EIO;
  offset+=size;p+=size;n-=size;
 }
 return 0;
}
static uint64_t cluster_offset(const struct storage_io *io,const struct fat32_layout *l,uint32_t cluster)
{ return ((uint64_t)l->data_start+cluster-2)*io->sector; }
static int put_dir(const struct storage_io *io,const struct fat32_layout *l,uint32_t cluster,const uint32_t first[4],const struct esp_file files[4])
{
 uint8_t block[4096];memset(block,0,sizeof(block));
 if(cluster==2) {
  dirent(block,"SILLICON   ",8,0,0);
  dirent(block+32,"EFI        ",16,3,0);
  dirent(block+64,"BOOT       ",16,5,0);
 } else {
  dirent(block,".          ",16,cluster,0);
  dirent(block+32,"..         ",16,cluster==4 ? 3 : 0,0);
  if(cluster==3) dirent(block+64,"BOOT       ",16,4,0);
  if(cluster==4) {
   dirent(block+64,"BOOTX64 EFI",32,first[0],(uint32_t)files[0].bytes);
   long_name(block+96,"limine.conf","LIMINE~1CNF");
   dirent(block+128,"LIMINE~1CNF",32,first[1],(uint32_t)files[1].bytes);
  }
  if(cluster==5) {
   dirent(block+64,"KERNEL  ELF",32,first[2],(uint32_t)files[2].bytes);
   dirent(block+96,"INIT    CPI",32,first[3],(uint32_t)files[3].bytes);
  }
 }
 int error=write_at(io,cluster_offset(io,l,cluster),block,io->sector);
 if(!error) error=check_at(io,cluster_offset(io,l,cluster),block,io->sector);
 return error;
}
int esp_format(const struct storage_io *io,uint32_t hidden,uint32_t serial,const struct esp_file files[4])
{
 struct fat32_layout l;int error=fat32_plan(io,&l);
 if(error) return error;
 if(!files || !io->write || !io->read || !io->flush) return -EINVAL;
 uint32_t first[4],count[4],next=6;
 for(unsigned i=0;i<4;i++) {
  if(!files[i].read || !files[i].bytes || files[i].bytes>UINT32_MAX) return -EINVAL;
  count[i]=(uint32_t)((files[i].bytes+io->sector-1)/io->sector);
  if(count[i]>l.clusters || next-2>l.clusters-count[i]) return -ENOSPC;
  first[i]=next;next+=count[i];
 }
 size_t fat_bytes=(size_t)l.fat_sectors*io->sector;
 uint8_t *fat=calloc(1,fat_bytes);if(!fat) return -ENOMEM;
 storage_le32(fat,0x0ffffff8);storage_le32(fat+4,0x0fffffff);
 for(uint32_t i=2;i<6;i++) storage_le32(fat+i*4,EOC);
 for(unsigned i=0;i<4;i++) for(uint32_t c=0;c<count[i];c++)
  storage_le32(fat+(size_t)(first[i]+c)*4,c+1==count[i] ? EOC : first[i]+c+1);
 uint8_t block[4096];memset(block,0,sizeof(block));
 /* Invalidate both boot records before changing allocation metadata. */
 error=write_at(io,0,block,io->sector);
 if(!error) error=write_at(io,(uint64_t)6*io->sector,block,io->sector);
 if(!error) error=io->flush(io->context);
 if(error) goto out;
 for(uint32_t i=1;i<RESERVED;i++) if((error=write_at(io,(uint64_t)i*io->sector,block,io->sector))) goto out;
 for(unsigned copy=0;copy<2;copy++) {
  uint64_t offset=((uint64_t)RESERVED+copy*l.fat_sectors)*io->sector;
  if((error=write_at(io,offset,fat,fat_bytes))) goto out;
  if((error=check_at(io,offset,fat,fat_bytes))) goto out;
 }
 for(uint32_t cluster=2;cluster<6;cluster++)
  if((error=put_dir(io,&l,cluster,first,files))) goto out;
 for(unsigned i=0;i<4;i++) {
  uint64_t offset=0,allocated=(uint64_t)count[i]*io->sector;
  while(offset<allocated) {
   size_t n=(size_t)(allocated-offset>sizeof(block) ? sizeof(block) : allocated-offset);
   size_t actual=(size_t)(files[i].bytes-offset>n ? n : files[i].bytes-offset);
   memset(block,0,n);
   if((error=files[i].read(files[i].context,offset,block,actual))) goto out;
   uint64_t target=cluster_offset(io,&l,first[i])+offset;
   if((error=write_at(io,target,block,n)) || (error=check_at(io,target,block,n))) goto out;
   offset+=n;
  }
 }
 if((error=io->flush(io->context))) goto out;
 /* FSInfo and its backup contain the exact allocation count. */
 memset(block,0,sizeof(block));
 storage_le32(block,0x41615252);storage_le32(block+484,0x61417272);
 storage_le32(block+488,l.clusters-(next-2));
 storage_le32(block+492,next-2<l.clusters ? next : UINT32_MAX);
 storage_le32(block+508,0xaa550000);
 error=write_at(io,io->sector,block,io->sector);
 if(!error) error=write_at(io,(uint64_t)7*io->sector,block,io->sector);
 if(error) goto out;
 memset(block,0,sizeof(block));
 block[0]=0xeb;block[1]=0x58;block[2]=0x90;memcpy(block+3,"OPENOS  ",8);
 storage_le16(block+11,(uint16_t)io->sector);block[13]=1;
 storage_le16(block+14,RESERVED);block[16]=2;block[21]=0xf8;
 storage_le16(block+24,63);storage_le16(block+26,255);storage_le32(block+28,hidden);
 storage_le32(block+32,l.sectors);storage_le32(block+36,l.fat_sectors);
 storage_le32(block+44,2);storage_le16(block+48,1);storage_le16(block+50,6);
 block[64]=0x80;block[66]=0x29;storage_le32(block+67,serial);
 memcpy(block+71,"SILLICON   ",11);memcpy(block+82,"FAT32   ",8);
 block[510]=0x55;block[511]=0xaa;
 error=write_at(io,(uint64_t)6*io->sector,block,io->sector);
 if(!error) error=io->flush(io->context);
 if(!error) error=write_at(io,0,block,io->sector);
 if(!error) error=io->flush(io->context);
 if(!error) error=check_at(io,0,block,io->sector);
 if(!error) error=check_at(io,(uint64_t)6*io->sector,block,io->sector);
out:
 free(fat);return error;
}
