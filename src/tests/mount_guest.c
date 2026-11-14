#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void)
{
 assert(mkdir("/mnt",0700)==0);
 struct silliconfs_args args={"/dev/ada0"};
 assert(mount("silliconfs","/mnt/",0,&args)==0);
 int fd=open("/mnt/probe",O_RDONLY);
 if(fd>=0) {
  char data[16]={0};assert(read(fd,data,7)==7 && strcmp(data,"durable")==0);
  struct stat st;assert(fstat(fd,&st)==0 && st.st_uid==1000 && st.st_gid==1000 && (st.st_mode&0777)==0640);
  close(fd);assert(unmount("/mnt",0)==0);
  puts("MOUNT PHASE2 PASS: persisted contents and ownership");
 } else {
  fd=open("/mnt/probe",O_RDWR|O_CREAT,0600);assert(fd>=0);
  assert(write(fd,"durable",7)==7 && fsync(fd)==0);
  assert(fchown(fd,1000,1000)==0 && fchmod(fd,0640)==0);
  assert(unmount("/mnt",0)==-1 && errno==EBUSY);
  close(fd);
  assert(chdir("/mnt")==0);
  assert(unmount("/mnt",0)==-1 && errno==EBUSY);
  assert(chdir("/")==0);
  int raw=open("/dev/ada0",O_RDWR);assert(raw>=0);
  unsigned char sector[512]={0};
  assert(write(raw,sector,sizeof(sector))==-1 && errno==EBUSY);
  assert(ioctl(raw,DIOCREREADGPT)==-1 && errno==EBUSY);
  close(raw);
  assert(sync_all()==0 && unmount("/mnt",0)==0);
  assert(seteuid(1000)==0);
  assert(mount("silliconfs","/mnt",0,&args)==-1 && errno==EPERM);
  assert(seteuid(0)==0);
  puts("MOUNT PHASE1 PASS: fsync, busy FD/CWD, raw write protection, root permissions");
 }
 fflush(stdout);for(;;)sleep(1);
}
