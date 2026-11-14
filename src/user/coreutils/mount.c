#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
int main(int argc,char **argv)
{
 if(argc!=5 || strcmp(argv[1],"-t") || strcmp(argv[2],"silliconfs")) {
  fputs("usage: mount -t silliconfs /dev/ada0p2 /mnt\n",stderr);return 2;
 }
 struct silliconfs_args args={argv[3]};
 if(mount(argv[2],argv[4],0,&args)!=0) {fprintf(stderr,"mount: failed (errno %d)\n",errno);return 1;}
 return 0;
}
