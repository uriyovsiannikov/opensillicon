#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../storage/storage.h"
static int usage(void)
{
 fprintf(stderr,"usage: newfs_silliconfs [-N | -F] [-i inodes] /dev/ada0p2\n"
  "  -N preview (default); -F explicitly authorizes formatting the device\n");
 return 2;
}
int main(int argc,char **argv)
{
 int format=0, mode_seen=0; uint32_t inodes=1024; const char *path=NULL;
 for(int i=1;i<argc;i++) {
  if(strcmp(argv[i],"-N")==0 || strcmp(argv[i],"-F")==0) {
   if(mode_seen++) return usage();
   format=strcmp(argv[i],"-F")==0;
  } else if(strcmp(argv[i],"-i")==0) {
   if(++i==argc) return usage();
   uint64_t n=0; const char *p=argv[i]; if(!*p) return usage();
   for(;*p;p++) { if(*p<'0'||*p>'9'||n>(UINT32_MAX-(unsigned)(*p-'0'))/10) return usage(); n=n*10+(unsigned)(*p-'0'); }
   if(!n) return usage();
   inodes=(uint32_t)n;
  } else if(argv[i][0]=='-' || path) return usage();
  else path=argv[i];
 }
 if(!path) return usage();
 struct storage_device device; int error=storage_open(&device,path,format);
 if(error) { fprintf(stderr,"newfs_silliconfs: cannot open %s (errno %d)\n",path,-error); return 1; }
 struct sfs_layout plan; error=sfs_plan(&device.io,inodes,&plan);
 if(!error) {
  printf("%s: %llu bytes, %u blocks, %u inodes, %u-byte sectors\n",path,
   (unsigned long long)device.io.bytes,plan.blocks,plan.inodes,device.io.sector);
  printf("metadata blocks: %u; first data block: %u\n",plan.data_start,plan.data_start);
  if(format) error=sfs_format(&device.io,inodes);
  else puts("Preview only. Use -F to format this device.");
 }
 int close_error=storage_close(&device); if(!error) error=close_error;
 if(error) { fprintf(stderr,"newfs_silliconfs: failed (errno %d)\n",-error); return 1; }
 if(format) puts("SilliconFS created and flushed.");
 return 0;
}
