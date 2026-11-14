#ifndef OPENSILLICON_COMMON_UUID_H
#define OPENSILLICON_COMMON_UUID_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* GPT stores the first three UUID fields in little-endian byte order. */
static inline bool uuid_parse(const char *text,size_t length,uint8_t wire[16])
{
 static const unsigned order[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
 if(!text || length!=36) return false;
 unsigned cursor=0;
 for(unsigned i=0;i<16;i++) {
  if(i==4||i==6||i==8||i==10) if(text[cursor++]!='-') return false;
  unsigned byte=0;
  for(unsigned j=0;j<2;j++) {
   unsigned char c=(unsigned char)text[cursor++];unsigned n;
   if(c>='0'&&c<='9') n=c-'0';
   else if(c>='a'&&c<='f') n=c-'a'+10;
   else if(c>='A'&&c<='F') n=c-'A'+10;
   else return false;
   byte=byte*16+n;
  }
  wire[order[i]]=(uint8_t)byte;
 }
 return true;
}
static inline void uuid_format(const uint8_t wire[16],char text[37])
{
 static const unsigned order[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
 static const char hex[]="0123456789abcdef";
 unsigned cursor=0;
 for(unsigned i=0;i<16;i++) {
  if(i==4||i==6||i==8||i==10) text[cursor++]='-';
  text[cursor++]=hex[wire[order[i]]>>4];text[cursor++]=hex[wire[order[i]]&15];
 }
 text[cursor]=0;
}
#endif
