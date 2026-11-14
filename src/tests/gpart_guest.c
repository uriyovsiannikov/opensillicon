#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
static int run(char **argv)
{
 pid_t pid=fork(); assert(pid>=0);
 if(pid==0) { execv(argv[0],argv); _exit(127); }
 int status=0; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status));
 return WEXITSTATUS(status);
}
int main(void)
{
 unsigned char bytes[4096];
 if(access("/dev/ada0p2",F_OK)==0) {
  int64_t size=0;
  int fd=open("/dev/ada0p2",O_RDONLY); assert(fd>=0);
  assert(ioctl(fd,DIOCGMEDIASIZE,&size)==0 && size>64*1024*1024);
  char *format[]={"/bin/newfs_silliconfs","-F","-i","128","/dev/ada0p2",NULL};
  assert(run(format)==0);
  assert(read(fd,bytes,512)==512 && memcmp(bytes,"SILFS1\0\0",8)==0);close(fd);
  puts("GPART PHASE2 PASS: partition discovery and native newfs");
 } else {
  int fd=open("/dev/ada0",O_RDONLY);assert(fd>=0);
  assert(read(fd,bytes,512)==512 && bytes[0]==0xcc);
  char *preview[]={"/bin/gpart","bootlayout","-N","-e","64","/dev/ada0",NULL};
  assert(run(preview)==0);
  assert(lseek(fd,0,SEEK_SET)==0 && read(fd,bytes,512)==512 && bytes[0]==0xcc);
  char *create[]={"/bin/gpart","bootlayout","-F","-e","64","/dev/ada0",NULL};
  assert(run(create)==0);
  assert(lseek(fd,0,SEEK_SET)==0 && read(fd,bytes,512)==512 && bytes[510]==0x55 && bytes[511]==0xaa);
  close(fd);puts("GPART PHASE1 PASS: preview and GPT creation");
 }
 fflush(stdout);
 for(;;) sleep(1);
}
