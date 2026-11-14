#include "storage.h"
void storage_le16(void *out,uint16_t value)
{ uint8_t *p=out; for(unsigned i=0;i<2;i++) p[i]=(uint8_t)(value>>(8*i)); }
void storage_le32(void *out,uint32_t value)
{ uint8_t *p=out; for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(8*i)); }
void storage_le64(void *out,uint64_t value)
{ uint8_t *p=out; for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(value>>(8*i)); }
uint32_t storage_crc32_update(uint32_t previous,const void *buffer,size_t size)
{
 const uint8_t *p=buffer; uint32_t crc=~previous;
 while(size--) {
  crc^=*p++;
  for(unsigned i=0;i<8;i++) crc=(crc>>1)^((0U-(crc&1U))&0xedb88320U);
 }
 return ~crc;
}

uint32_t storage_crc32(const void *buffer,size_t size)
{ return storage_crc32_update(0,buffer,size); }
