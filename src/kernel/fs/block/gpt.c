#include <string.h>
#include "kernel/gpt.h"
#include "kernel/heap.h"
#include "kernel/errno.h"
#include "kernel/log.h"

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint64_t le64(const uint8_t *p)
{ return le32(p) | (uint64_t)le32(p+4)<<32; }
static bool zero_guid(const uint8_t *p)
{ static const uint8_t zero[16]; return memcmp(p,zero,16)==0; }
static uint32_t crc32(const uint8_t *p, size_t n)
{
 uint32_t crc=UINT32_MAX;
 while(n--) {
  crc^=*p++;
  for(unsigned int i=0;i<8;i++) crc=(crc>>1)^((0U-(crc&1U))&0xedb88320U);
 }
 return ~crc;
}
static void *aligned_buffer(size_t size, void **allocation)
{
 *allocation=kheap_alloc(size+4095U);
 return *allocation ? (void *)(((uintptr_t)*allocation+4095U)&~(uintptr_t)4095U) : NULL;
}

static int read_copy(struct block_device *disk, uint64_t lba,
 struct gpt_table *table)
{
 void *allocation=NULL, *array_allocation=NULL;
 uint8_t *h=aligned_buffer(disk->logical_sector_size,&allocation);
 int error=-EINVAL;
 if(h==NULL) return -ENOMEM;
 if(block_read(disk,lba,1,h)!=0) { error=-EIO; goto out; }
 uint32_t header_size=le32(h+12), stored_crc=le32(h+16);
 if(memcmp(h,"EFI PART",8)!=0 || le32(h+8)!=0x10000U ||
    header_size<92 || header_size>disk->logical_sector_size || le32(h+20)!=0)
  goto out;
 memset(h+16,0,4);
 if(crc32(h,header_size)!=stored_crc || le64(h+24)!=lba ||
    le64(h+32)!=(lba==1 ? disk->sector_count-1 : 1) || zero_guid(h+56)) goto out;
 uint64_t first=le64(h+40), last=le64(h+48), array_lba=le64(h+72);
 uint32_t entries=le32(h+80), entry_size=le32(h+84), array_crc=le32(h+88);
 if(entries==0 || entries>GPT_ENTRY_MAX || entry_size<128 || entry_size>1024 ||
    (entry_size&(entry_size-1))!=0) goto out;
 size_t bytes=(size_t)entries*entry_size;
 size_t sectors=(bytes+disk->logical_sector_size-1)/disk->logical_sector_size;
 size_t reserved=(16384U+disk->logical_sector_size-1)/disk->logical_sector_size;
 if(sectors>reserved) reserved=sectors;
 if(first<2+reserved || first>last || last>=disk->sector_count-1-reserved ||
    array_lba>=disk->sector_count || sectors>disk->sector_count-array_lba) goto out;
 if(lba==1) {
  if(array_lba<2 || array_lba+sectors>first) goto out;
 } else if(array_lba<=last || array_lba+sectors>lba) goto out;
 uint8_t *array=aligned_buffer(sectors*disk->logical_sector_size,&array_allocation);
 if(array==NULL) { error=-ENOMEM; goto out; }
 if(block_read(disk,array_lba,(uint32_t)sectors,array)!=0) { error=-EIO; goto out; }
 if(crc32(array,bytes)!=array_crc) goto out;
 memset(table,0,sizeof(*table));
 table->first_usable=first; table->last_usable=last;
 table->entry_count=entries; table->entry_size=entry_size; table->array_crc=array_crc;
 memcpy(table->disk_uuid,h+56,16);
 for(uint32_t i=0;i<entries;i++) {
  const uint8_t *e=array+(size_t)i*entry_size;
  if(zero_guid(e)) continue;
  uint64_t start=le64(e+32), end=le64(e+40);
  if(zero_guid(e+16) || start>end || start<first || end>last) goto out;
  for(uint32_t j=0;j<table->count;j++) {
   const struct gpt_partition *previous=&table->partitions[j];
   if((start<=previous->last && end>=previous->first) ||
      memcmp(previous->uuid,e+16,16)==0) goto out;
  }
  struct gpt_partition *p=&table->partitions[table->count++];
  p->first=start; p->last=end; p->number=i+1;
  memcpy(p->type,e,16); memcpy(p->uuid,e+16,16);
 }
 error=(int)table->count;
out:
 if(array_allocation) (void)kheap_free(array_allocation);
 (void)kheap_free(allocation);
 return error;
}

int gpt_read(struct block_device *disk, struct gpt_table *table)
{
 if(disk==NULL || table==NULL || !disk->registered || disk->parent!=NULL ||
    disk->sector_count<70) return -EINVAL;
 memset(table,0,sizeof(*table));
 void *allocation;
 uint8_t *mbr=aligned_buffer(disk->logical_sector_size,&allocation);
 if(mbr==NULL) return -ENOMEM;
 int result=block_read(disk,0,1,mbr);
 if(result!=0) { (void)kheap_free(allocation); return result; }
 bool protective=false, other=false;
 for(unsigned int i=0;i<4;i++) {
  const uint8_t *record=mbr+446+i*16;
  if(record[4]==0xee && le32(record+8)==1) protective=true;
  else if(record[4]!=0) other=true;
 }
 bool signature=mbr[510]==0x55 && mbr[511]==0xaa;
 (void)kheap_free(allocation);
 if(!protective) return 0;
 if(!signature || other) return -EINVAL;
 struct gpt_table *backup=kheap_alloc(sizeof(*backup));
 if(backup==NULL) return -ENOMEM;
 int primary_status=read_copy(disk,1,table);
 int backup_status=read_copy(disk,disk->sector_count-1,backup);
 if(primary_status>=0 && backup_status>=0) {
  /* Both copies must describe the same partitions and usable interval. */
  if(memcmp(table,backup,sizeof(*table))!=0) result=-EINVAL;
  else result=primary_status;
 } else if(primary_status>=0) {
  table->degraded=true; result=primary_status;
 } else if(backup_status>=0) {
  *table=*backup; table->backup=true; table->degraded=true; result=backup_status;
 } else result=primary_status;
 if(result<0) memset(table,0,sizeof(*table));
 (void)kheap_free(backup);
 return result;
}

void gpt_discover(void)
{
 static struct block_device partitions[BLOCK_DEVICE_MAX];
 static size_t used;
 size_t disks=block_device_count();
 struct gpt_table *table=kheap_alloc(sizeof(*table));
 if(table==NULL) { klog("GPT: allocation failed\n"); return; }
 for(size_t i=0;i<disks;i++) {
  struct block_device *disk=block_device_at(i);
  if(disk->parent) continue;
  bool already=false;
  for(size_t j=0;j<block_device_count();j++)
   if(block_device_at(j)->parent==disk) already=true;
  if(already) continue;
  int count=gpt_read(disk,table);
  if(count<0) { klog("GPT: invalid/unreadable table on "); klog(disk->name); klog("\n"); continue; }
  /* Reserve ram0's slot before publishing any partition from this disk. */
  if((size_t)count>=BLOCK_DEVICE_MAX-block_device_count()) {
   klog("GPT: partition registry full\n"); continue;
  }
  for(int j=0;j<count;j++) {
   const struct gpt_partition *p=&table->partitions[j];
   struct block_device *device=&partitions[used];
   int error=block_partition_register(device,disk,p->first,p->last-p->first+1,p->number);
   if(error!=0) { klog("GPT: partition registration failed\n"); break; }
   memcpy(device->partition_uuid,p->uuid,16);
   memcpy(device->partition_type,p->type,16);
   used++;
  }
  if(table->degraded) { klog("GPT: using surviving GPT copy on "); klog(disk->name); klog("\n"); }
 }
 (void)kheap_free(table);
}
