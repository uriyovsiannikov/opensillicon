#include "abi/signal.h"
#include "abi/uio.h"
#include "abi/poll.h"
#include "abi/process_info.h"
#include "abi/exec.h"
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/opensillicon.h>
#include <limits.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <poll.h>
#include <curses.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <netdb.h>
#define hidden __attribute__((visibility("hidden")))
#include "../src/network/lookup.h"
#undef hidden
#include "abi/syscall.h"
#include "abi/fcntl.h"
#include "abi/dirent.h"
#include "abi/inet.h"
#include "abi/socket.h"
#include "abi/unix_socket.h"
#include <sys/icmp.h>
#include "abi/ioctl.h"
extern long __os_raw(long,long,long,long,long,long,long);
static _Thread_local int tls = 123;
static int constructed;
__attribute__((constructor)) static void construct(void) { constructed=42; }
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr,"MUSL_FAIL line %d: %s errno=%d\n",__LINE__,#condition,errno); return __LINE__%254+1; } } while (0)
static int large_iovec_check(void)
{
 CHECK(IOV_MAX==OS_IOV_MAX && UIO_MAXIOV==OS_IOV_MAX);
 CHECK(sysconf(_SC_IOV_MAX)==OS_IOV_MAX);
 const size_t length=256*1024+37;
 unsigned char *data=malloc(length), *result=malloc(length);
 CHECK(data && result);
 for(size_t i=0;i<length;i++) data[i]=(unsigned char)(i*17+3);
 int fd=open("/musl-large-io",O_CREAT|O_EXCL|O_RDWR,0600);
 CHECK(fd>=0);
 struct iovec vectors[2]={{data,17},{data+17,length-17}};
 ssize_t amount=writev(fd,vectors,2);
 CHECK(amount>17 && (size_t)amount<length);
 size_t done=(size_t)amount;
 while(done<length) {
  struct iovec rest={data+done,length-done};
  amount=writev(fd,&rest,1); CHECK(amount>0 && (size_t)amount<=length-done);
  done+=(size_t)amount;
 }
 CHECK(lseek(fd,0,SEEK_CUR)==(off_t)length && lseek(fd,0,SEEK_SET)==0);
 vectors[0]=(struct iovec){result,13};
 vectors[1]=(struct iovec){result+13,length-13};
 amount=readv(fd,vectors,2); CHECK(amount>13 && (size_t)amount<length);
 done=(size_t)amount;
 while(done<length) {
  struct iovec rest={result+done,length-done};
  amount=readv(fd,&rest,1); CHECK(amount>0 && (size_t)amount<=length-done);
  done+=(size_t)amount;
 }
 CHECK(!memcmp(data,result,length));
 CHECK(readv(fd,vectors,2)==0);
 struct iovec overflow[2]={{data,(size_t)INT64_MAX},{data,1}};
 CHECK(writev(fd,overflow,2)==-1 && errno==EINVAL);
 CHECK(readv(fd,overflow,2)==-1 && errno==EINVAL);
 CHECK(lseek(fd,0,SEEK_CUR)==(off_t)length);
 CHECK(__os_raw(SYSCALL_WRITEV,fd,1,1,0,0,0)==-EFAULT);
 CHECK(writev(fd,NULL,0)==0 && readv(fd,NULL,0)==0);
 struct iovec limit[OS_IOV_MAX+1]={0};
 CHECK(writev(fd,limit,OS_IOV_MAX)==0 && readv(fd,limit,OS_IOV_MAX)==0);
 CHECK(writev(fd,limit,OS_IOV_MAX+1)==-1 && errno==EINVAL);
 CHECK(readv(fd,limit,OS_IOV_MAX+1)==-1 && errno==EINVAL);
 CHECK(close(fd)==0);
 fd=open("/musl-large-io",O_RDWR|O_TRUNC); CHECK(fd>=0);
 for(int native=0;native<2;native++) {
  CHECK(lseek(fd,0,SEEK_SET)==0);
  done=0;
  while(done<length) {
   amount=native ? __os_raw(SYSCALL_WRITE,fd,(long)(data+done),length-done,0,0,0) :
       write(fd,data+done,length-done);
   CHECK(amount>0 && (size_t)amount<=length-done);
   if(!done) CHECK(amount>4096 && (size_t)amount<length);
   done+=(size_t)amount;
  }
  CHECK(lseek(fd,0,SEEK_SET)==0);
  memset(result,0,length);
  done=0;
  while(done<length) {
   amount=native ? __os_raw(SYSCALL_READ,fd,(long)(result+done),length-done,0,0,0) :
       read(fd,result+done,length-done);
   CHECK(amount>0 && (size_t)amount<=length-done);
   if(!done) CHECK(amount>4096 && (size_t)amount<length);
   done+=(size_t)amount;
  }
  CHECK(!memcmp(data,result,length));
 }
 CHECK(__os_raw(SYSCALL_READ,fd,1,4097,0,0,0)==-EFAULT);
 CHECK(__os_raw(SYSCALL_WRITE,fd,1,4097,0,0,0)==-EFAULT);
 CHECK(__os_raw(SYSCALL_READ,fd,(long)result,UINT64_MAX,0,0,0)==-EINVAL);
 CHECK(__os_raw(SYSCALL_WRITE,fd,(long)data,UINT64_MAX,0,0,0)==-EINVAL);
 CHECK(lseek(fd,0,SEEK_CUR)==(off_t)length && close(fd)==0);
 FILE *file=fopen("/musl-large-io","w+"); CHECK(file);
 CHECK(fwrite("prefix",1,6,file)==6);
 CHECK(fwrite(data,1,length,file)==length && fflush(file)==0);
 CHECK(ftell(file)==(long)length+6 && fseek(file,6,SEEK_SET)==0);
 memset(result,0,length);
 CHECK(fread(result,1,length,file)==length && !memcmp(data,result,length));
 CHECK(fclose(file)==0 && unlink("/musl-large-io")==0);
 free(result); free(data);
 return 0;
}
static int files_check(void)
{
 char cwd[256];
 CHECK(getcwd(cwd,sizeof cwd) && !strcmp(cwd,"/"));
 CHECK(mkdir("/musl-files",0700)==0);
 CHECK(chdir("/musl-files")==0);
 char *allocated_cwd=getcwd(NULL,0);
 CHECK(allocated_cwd && !strcmp(allocated_cwd,"/musl-files"));
 free(allocated_cwd);
 int fd=open("data",O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
 CHECK(fd>=0);
 CHECK(fcntl(fd,F_GETFD)&FD_CLOEXEC);
 CHECK((fcntl(fd,F_GETFL)&O_ACCMODE)==O_RDWR);
 CHECK(fcntl(fd,F_SETFD,0)==0 && fcntl(fd,F_GETFD)==0);
 CHECK(fcntl(fd,F_SETFD,2)==-1 && errno==EINVAL && fcntl(fd,F_GETFD)==0);
 CHECK(__os_raw(SYSCALL_FCNTL,fd,OS_F_SETFD,1ULL<<32,0,0,0)==-EINVAL);
 CHECK(fcntl(fd,F_GETFD)==0);
 CHECK(__os_raw(SYSCALL_FCNTL,fd,8,0,0,0,0)==-EOPNOTSUPP);
 CHECK(__os_raw(SYSCALL_FCNTL,fd,0x7fffffff,0,0,0,0)==-EOPNOTSUPP);
 CHECK(fcntl(fd,F_SETFL,O_NONBLOCK)==0);
 CHECK(fcntl(fd,F_GETFL)&O_NONBLOCK);
 CHECK(write(fd,"abc",3)==3);
 struct stat st;
 CHECK(fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size==3);
 struct { uint64_t before; struct stat value; uint64_t after; } guarded_stat={
     .before=0x123456789abcdef0ULL,.after=0xfedcba9876543210ULL};
 CHECK(fstatat(AT_FDCWD,"data",&guarded_stat.value,0)==0);
 CHECK(!memcmp(&st,&guarded_stat.value,sizeof st) &&
     guarded_stat.before==0x123456789abcdef0ULL && guarded_stat.after==0xfedcba9876543210ULL);
 CHECK(__os_raw(SYSCALL_FSTAT,fd,1,0,0,0,0)==-EFAULT);
 CHECK(__os_raw(SYSCALL_FSTATAT,AT_FDCWD,(long)"data",1,0,0,0)==-EFAULT);
 unsigned char *stat_pages=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 CHECK(stat_pages!=MAP_FAILED && mprotect(stat_pages+4096,4096,PROT_NONE)==0);
 unsigned char *partial_stat=stat_pages+4096-80;
 memset(partial_stat,0x5a,80);
 CHECK(__os_raw(SYSCALL_FSTAT,fd,(long)partial_stat,0,0,0,0)==-EFAULT);
 CHECK(__os_raw(SYSCALL_FSTATAT,AT_FDCWD,(long)"data",(long)partial_stat,0,0,0)==-EFAULT);
 for(size_t i=0;i<80;i++) CHECK(partial_stat[i]==0x5a);
 CHECK(munmap(stat_pages,8192)==0);
 int copy=dup(fd);
 CHECK(copy>=0 && lseek(copy,0,SEEK_SET)==0);
 char buf[4]={0};
 CHECK(read(fd,buf,3)==3 && !strcmp(buf,"abc"));
 CHECK(close(copy)==0 && close(fd)==0);
 errno=0;
 CHECK(open("data",O_CREAT|O_EXCL|O_WRONLY,0600)==-1 && errno==EEXIST);
 CHECK(stat("data",&st)==0 && st.st_size==3);
 CHECK(open("data",O_PATH)==-1 && errno==EINVAL);
 CHECK(open("path-create",O_PATH|O_CREAT|O_WRONLY,0600)==-1 && errno==EINVAL);
 CHECK(stat("path-create",&st)==-1 && errno==ENOENT);
 char canonical[256];
 CHECK(realpath("./data",canonical)==canonical && !strcmp(canonical,"/musl-files/data"));
 CHECK(readlink("data",canonical,sizeof canonical)==-1 && errno==EINVAL);
 CHECK(realpath("missing",canonical)==NULL && errno==ENOENT);
 CHECK(realpath("data/child",canonical)==NULL && errno==ENOTDIR);
 CHECK(lstat("data",&st)==0 && S_ISREG(st.st_mode));
 CHECK(chmod("data",0640)==0);
 CHECK(stat("data",&st)==0 && (st.st_mode&0777)==0640);
 CHECK(access("data",R_OK)==0);
 FILE *f=fopen("data","w+");
 CHECK(f);
 char payload[10000];
 for(size_t i=0;i<sizeof payload;i++) payload[i]=(char)(i%127);
 CHECK(fwrite(payload,1,sizeof payload,f)==sizeof payload && fflush(f)==0);
 CHECK(ftell(f)==sizeof payload && fseek(f,0,SEEK_SET)==0);
 char result[10000];
 CHECK(fread(result,1,sizeof result,f)==sizeof result);
 CHECK(!memcmp(result,payload,sizeof result) && fclose(f)==0);
 CHECK(mkdir("entry-dir",0700)==0);
 int raw_dir=open(".",O_RDONLY|O_DIRECTORY), fresh_dir=open(".",O_RDONLY|O_DIRECTORY);
 CHECK(raw_dir>=0 && fresh_dir>=0);
 struct { uint64_t words[OS_GETDENTS_MAX/8]; uint64_t guard; } records;
 uint64_t fresh_records[OS_GETDENTS_MAX/8];
 memset(&records,0xff,sizeof records);
 CHECK(__os_raw(SYSCALL_GETDENTS,raw_dir,(long)records.words,1,0,0,0)==-EINVAL);
 CHECK(__os_raw(SYSCALL_GETDENTS,raw_dir,1,OS_GETDENTS_MAX,0,0,0)==-EFAULT);
 CHECK(__os_raw(SYSCALL_GETDENTS,raw_dir,(long)records.words,OS_GETDENTS_MAX+1,0,0,0)==-EINVAL);
 long record_bytes=__os_raw(SYSCALL_GETDENTS,raw_dir,(long)records.words,OS_GETDENTS_MAX,0,0,0);
 CHECK(record_bytes>0 && record_bytes<=OS_GETDENTS_MAX);
 CHECK(__os_raw(SYSCALL_GETDENTS,fresh_dir,(long)fresh_records,OS_GETDENTS_MAX,0,0,0)==record_bytes);
 CHECK(!memcmp(records.words,fresh_records,(size_t)record_bytes) && records.guard==UINT64_MAX);
 for(size_t i=(size_t)record_bytes;i<sizeof records.words;i++)
  CHECK(((unsigned char *)records.words)[i]==0xff);
 size_t record_position=0; int raw_files=0, raw_directories=0; int64_t last_offset=0;
 while(record_position<(size_t)record_bytes) {
  struct os_dirent_record *record=(void *)((char *)records.words+record_position);
  size_t header=offsetof(struct os_dirent_record,name);
  CHECK((size_t)record_bytes-record_position>=header+1);
  CHECK(record->record_length>=header+1 && record->record_length%OS_DIRENT_ALIGNMENT==0 &&
      record->record_length<=(size_t)record_bytes-record_position);
  CHECK(record->inode!=0 && record->offset>last_offset);
  last_offset=record->offset;
  char *end=memchr(record->name,0,record->record_length-header);
  CHECK(end!=NULL && end!=record->name);
  if(!strcmp(record->name,"data")) { CHECK(record->type==OS_DT_REG); raw_files++; }
  if(!strcmp(record->name,"entry-dir")) { CHECK(record->type==OS_DT_DIR); raw_directories++; }
  for(char *padding=end+1;padding<(char *)record+record->record_length;padding++) CHECK(*padding==0);
  record_position+=record->record_length;
 }
 CHECK(record_position==(size_t)record_bytes && raw_files==1 && raw_directories==1);
 CHECK(__os_raw(SYSCALL_GETDENTS,raw_dir,(long)records.words,OS_GETDENTS_MAX,0,0,0)==0);
 CHECK(close(raw_dir)==0 && close(fresh_dir)==0 && rmdir("entry-dir")==0);
 DIR *dir=opendir(".");
 CHECK(dir);
 CHECK(fcntl(dirfd(dir),F_GETFD)&FD_CLOEXEC);
 CHECK(fstatat(dirfd(dir),"data",&st,0)==0 && st.st_size==sizeof payload);
 int found=0;
 struct dirent *entry;
 errno=0;
 while((entry=readdir(dir))) if(!strcmp(entry->d_name,"data")) found++;
 CHECK(found==1 && errno==0);
 rewinddir(dir);
 found=0;
 while((entry=readdir(dir))) if(!strcmp(entry->d_name,"data")) found++;
 CHECK(found==1 && closedir(dir)==0);
 /* Unsupported flags must fail before creating or truncating a file. */
 errno=0;
 CHECK(open("unsupported",O_CREAT|O_WRONLY|O_SYNC,0600)==-1 && errno==EOPNOTSUPP);
 CHECK(stat("unsupported",&st)==-1 && errno==ENOENT);
 CHECK(unlink("data")==0 && chdir("/")==0 && rmdir("/musl-files")==0);
 return 0;
}
static int openat_check(void)
{
 CHECK(mkdir("/musl-at",0700)==0 && mkdir("/musl-at/child",0700)==0);
 int dir=open("/musl-at",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
 CHECK(dir>=0 && (fcntl(dir,F_GETFD)&FD_CLOEXEC));
 int nested=openat(dir,"child",O_RDONLY|O_DIRECTORY);
 CHECK(nested>=0);
 CHECK(chdir("/tmp")==0);
 int fd=openat(nested,"file",O_RDWR|O_CREAT|O_EXCL|O_APPEND|O_CLOEXEC,0600);
 CHECK(fd>=0 && (fcntl(fd,F_GETFL)&O_APPEND) && (fcntl(fd,F_GETFD)&FD_CLOEXEC));
 CHECK(write(fd,"A",1)==1 && lseek(fd,0,SEEK_SET)==0 && write(fd,"B",1)==1);
 CHECK(lseek(fd,0,SEEK_CUR)==2);
 int other=openat(dir,"child/file",O_WRONLY|O_APPEND);
 CHECK(other>=0 && write(other,"C",1)==1 && close(other)==0);
 CHECK(lseek(fd,0,SEEK_SET)==0 && write(fd,"D",1)==1);
 CHECK(fcntl(fd,F_SETFL,0)==0 && !(fcntl(fd,F_GETFL)&O_APPEND));
 CHECK(lseek(fd,0,SEEK_SET)==0 && write(fd,"X",1)==1);
 char text[8]={0};
 CHECK(lseek(fd,0,SEEK_SET)==0 && read(fd,text,sizeof text)==4 && !strcmp(text,"XBCD"));
 CHECK(openat(-1,"file",O_RDONLY)==-1 && errno==EBADF);
 CHECK(openat(fd,"file",O_RDONLY)==-1 && errno==ENOTDIR);
 CHECK(openat(dir,"",O_RDONLY)==-1 && errno==ENOENT);
 other=openat(-1,"/musl-at/child/file",O_RDONLY);
 CHECK(other>=0 && close(other)==0);
 FILE *stream=fopen("/musl-at/child/file","a");
 CHECK(stream && fputs("E",stream)>=0 && fclose(stream)==0);
 CHECK(lseek(fd,0,SEEK_SET)==0 && read(fd,text,sizeof text)==5 && !memcmp(text,"XBCDE",5));
 CHECK(close(fd)==0 && unlink("/musl-at/child/file")==0);
 CHECK(close(nested)==0 && rmdir("/musl-at/child")==0);
 CHECK(rmdir("/musl-at")==0 && mkdir("/musl-at",0700)==0);
 CHECK(openat(dir,"replacement",O_CREAT|O_RDWR,0600)==-1 && errno==ENOENT);
 CHECK(close(dir)==0 && rmdir("/musl-at")==0 && chdir("/")==0);
 return 0;
}
static int directory_capability_check(void)
{
 CHECK(mkdir("/musl-cap",0755)==0 && mkdir("/musl-cap/child",0755)==0);
 CHECK(chmod("/musl-cap/child",0777)==0);
 int dir=open("/musl-cap/child",O_RDONLY|O_DIRECTORY);
 CHECK(dir>=0 && chmod("/musl-cap",0000)==0);
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  if(setgid(1000) || setuid(1000)) _exit(98);
  int denied=open("/musl-cap/child/new",O_WRONLY|O_CREAT,0600);
  if(denied!=-1 || errno!=EACCES) _exit(99);
  int file=openat(dir,"new",O_WRONLY|O_CREAT|O_EXCL,0600);
  if(file<0 || write(file,"cap",3)!=3 || close(file)) _exit(100);
  _exit(0);
 }
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
 struct stat st;
 CHECK(fstatat(dir,"new",&st,0)==0 && st.st_uid==1000 && st.st_size==3);
 CHECK(chmod("/musl-cap",0755)==0 && close(dir)==0);
 CHECK(unlink("/musl-cap/child/new")==0 && rmdir("/musl-cap/child")==0 && rmdir("/musl-cap")==0);
 return 0;
}
static int concurrent_append_check(void)
{
 int file=open("/musl-append",O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
 CHECK(file>=0);
 pid_t children[2];
 for(unsigned id=0;id<2;id++) {
  children[id]=fork(); CHECK(children[id]>=0);
  if(!children[id]) {
   close(file);
   int writer=open("/musl-append",O_WRONLY|O_APPEND);
   if(writer<0) _exit(101);
   for(unsigned sequence=0;sequence<100;sequence++) {
    uint32_t record[2]={id,sequence};
    if(write(writer,record,sizeof record)!=sizeof record) _exit(102);
   }
   close(writer); _exit(0);
  }
 }
 for(unsigned id=0;id<2;id++) {
  int status;
  CHECK(waitpid(children[id],&status,0)==children[id] && WIFEXITED(status) && !WEXITSTATUS(status));
 }
 unsigned counts[2]={0};
 for(unsigned i=0;i<200;i++) {
  uint32_t record[2];
  CHECK(read(file,record,sizeof record)==sizeof record && record[0]<2);
  CHECK(record[1]==counts[record[0]]++);
 }
 char extra;
 CHECK(counts[0]==100 && counts[1]==100 && read(file,&extra,1)==0);
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  char descriptor[16]; snprintf(descriptor,sizeof descriptor,"%d",file);
  char *args[]={"musl-fd-check","--cloexec-check",descriptor,NULL};
  execv("/sbin/init",args); _exit(103);
 }
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==29);
 CHECK(close(file)==0 && unlink("/musl-append")==0);
 return 0;
}
static int duplication_check(void)
{
 int source=open("/musl-dup",O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
 CHECK(source>=0 && write(source,"AB",2)==2 && lseek(source,0,SEEK_SET)==0);
 int plain=fcntl(source,F_DUPFD,20), flagged=fcntl(source,F_DUPFD_CLOEXEC,20);
 CHECK(plain==20 && flagged==21);
 CHECK(fcntl(plain,F_GETFD)==0 && (fcntl(flagged,F_GETFD)&FD_CLOEXEC));
 char value;
 CHECK(read(plain,&value,1)==1 && value=='A' && read(flagged,&value,1)==1 && value=='B');
 CHECK(dup2(flagged,flagged)==flagged && (fcntl(flagged,F_GETFD)&FD_CLOEXEC));
 CHECK(dup3(flagged,flagged,O_CLOEXEC)==-1 && errno==EINVAL);
 CHECK(dup3(source,plain,O_NONBLOCK)==-1 && errno==EINVAL);
 CHECK(fcntl(plain,F_GETFD)==0);
 CHECK(dup3(source,plain,O_CLOEXEC)==plain && (fcntl(plain,F_GETFD)&FD_CLOEXEC));
 CHECK(dup3(-1,plain,0)==-1 && errno==EBADF);
 CHECK(dup2(-1,-1)==-1 && errno==EBADF);
 CHECK(dup3(-1,-1,0)==-1 && errno==EINVAL);
 CHECK(fcntl(plain,F_GETFD)&FD_CLOEXEC);
 CHECK(syscall(SYS_dup3,source,plain,0)==plain && fcntl(plain,F_GETFD)==0);
 CHECK(dup2(source,flagged)==flagged && fcntl(flagged,F_GETFD)==0);
 int lowest=dup(source); CHECK(lowest>=0 && fcntl(lowest,F_GETFD)==0);
 CHECK(lseek(lowest,0,SEEK_SET)==0 && read(flagged,&value,1)==1 && value=='A');
 CHECK(close(lowest)==0);
 CHECK(fcntl(source,F_DUPFD,-1)==-1 && errno==EINVAL);
 CHECK(fcntl(source,F_DUPFD_CLOEXEC,1000)==-1 && errno==EINVAL);
 CHECK(close(plain)==0 && close(flagged)==0);
 int channel[2]; CHECK(pipe2(channel,O_CLOEXEC|O_NONBLOCK)==0);
 CHECK((fcntl(channel[0],F_GETFD)&FD_CLOEXEC) && (fcntl(channel[1],F_GETFD)&FD_CLOEXEC));
 CHECK((fcntl(channel[0],F_GETFL)&(O_ACCMODE|O_NONBLOCK))==(O_RDONLY|O_NONBLOCK));
 CHECK((fcntl(channel[1],F_GETFL)&(O_ACCMODE|O_NONBLOCK))==(O_WRONLY|O_NONBLOCK));
 CHECK(read(channel[0],&value,1)==-1 && errno==EAGAIN);
 CHECK(write(channel[1],"Q",1)==1 && read(channel[0],&value,1)==1 && value=='Q');
 CHECK(close(channel[0])==0 && close(channel[1])==0);
 channel[0]=123; channel[1]=456;
 CHECK(pipe2(channel,O_APPEND)==-1 && errno==EINVAL && channel[0]==123 && channel[1]==456);
 CHECK(syscall(SYS_pipe2,1,O_CLOEXEC)==-1 && errno==EFAULT);
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  close(0); close(1); close(2);
  int file=open("/musl-dup",O_RDONLY);
  if(file!=0) _exit(105);
  int pair[2];
  if(pipe2(pair,O_CLOEXEC) || pair[0]!=1 || pair[1]!=2) _exit(106);
  close(pair[0]); close(pair[1]); close(file);
  if(fcntl(source,F_DUPFD_CLOEXEC,0)!=0) _exit(107);
  _exit(0);
 }
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
 int filled[128], count=0;
 while(count<128) {
  int fd=fcntl(source,F_DUPFD_CLOEXEC,0);
  if(fd<0)break;
  filled[count++]=fd;
 }
 CHECK(count<128 && errno==EMFILE);
 channel[0]=123; channel[1]=456;
 CHECK(pipe2(channel,0)==-1 && errno==EMFILE && channel[0]==123 && channel[1]==456);
 for(int i=0;i<count;i++) CHECK(close(filled[i])==0);
 CHECK(close(source)==0 && unlink("/musl-dup")==0);
 return 0;
}
static volatile sig_atomic_t signals_received, signal_mask_ok;
static void handle_usr1(int signo)
{
 sigset_t blocked;
 if(signo==SIGUSR1 && sigprocmask(SIG_SETMASK,NULL,&blocked)==0 &&
     sigismember(&blocked,SIGUSR1)==1) signal_mask_ok=1;
 signals_received++;
}
static int signals_check(void)
{
 struct sigaction action={.sa_handler=handle_usr1}, previous, queried;
 sigemptyset(&action.sa_mask);
 CHECK(sigaction(SIGUSR1,&action,&previous)==0);
 CHECK(sigaction(SIGUSR1,NULL,&queried)==0 && queried.sa_handler==handle_usr1);
 CHECK(raise(SIGUSR1)==0 && signals_received==1 && signal_mask_ok);
 sigset_t mask;
 sigemptyset(&mask);
 sigaddset(&mask,SIGUSR1);
 CHECK(sigprocmask(SIG_BLOCK,&mask,NULL)==0);
 CHECK(kill(getpid(),SIGUSR1)==0 && signals_received==1);
 CHECK(sigprocmask(SIG_UNBLOCK,&mask,NULL)==0 && signals_received==2);
 CHECK(sigaction(SIGUSR1,&previous,NULL)==0);
 CHECK(signal(SIGPIPE,SIG_IGN)!=SIG_ERR && raise(SIGPIPE)==0);
 action.sa_flags=SA_SIGINFO;
 CHECK(sigaction(SIGUSR1,&action,NULL)==-1 && errno==EOPNOTSUPP);
 return 0;
}
static volatile sig_atomic_t usr2_count, custom_mask_valid, usr1_inside;
static void handle_usr2(int signo)
{
 if(signo==SIGUSR2) usr2_count++;
}
static void handle_masked_usr1(int signo)
{
 sigset_t blocked;
 if(signo==SIGUSR1 && sigprocmask(SIG_SETMASK,NULL,&blocked)==0 &&
     sigismember(&blocked,SIGUSR1)==1 && sigismember(&blocked,SIGUSR2)==1 &&
     sigismember(&blocked,SIGKILL)==0) custom_mask_valid=1;
 if(kill(getpid(),SIGUSR2)==0 && usr2_count==0) usr1_inside=1;
}
static int signal_action_check(void)
{
 struct sigaction usr2={.sa_handler=handle_usr2}, usr1={.sa_handler=handle_masked_usr1};
 struct sigaction old1,old2,queried;
 sigemptyset(&usr1.sa_mask); sigemptyset(&usr2.sa_mask);
 sigaddset(&usr1.sa_mask,SIGUSR2); sigaddset(&usr1.sa_mask,SIGKILL);
 sigaddset(&usr1.sa_mask,SIGSTOP);
 CHECK(sigaction(SIGUSR2,&usr2,&old2)==0 && sigaction(SIGUSR1,&usr1,&old1)==0);
 CHECK(sigaction(SIGUSR1,NULL,&queried)==0 && sigismember(&queried.sa_mask,SIGUSR2)==1 &&
     sigismember(&queried.sa_mask,SIGKILL)==0 && sigismember(&queried.sa_mask,SIGSTOP)==0);
 CHECK(raise(SIGUSR1)==0 && custom_mask_valid && usr1_inside && usr2_count==1);
 sigset_t blocked;
 CHECK(sigprocmask(SIG_SETMASK,NULL,&blocked)==0 && !sigismember(&blocked,SIGUSR2));
 struct { struct os_signal_action action; uint64_t guard; } native={.guard=0xabcdef9876543210ULL};
 CHECK(__os_raw(SYSCALL_SIGACTION,SIGUSR1,0,(long)&native.action,0,0,0)==0);
 CHECK(native.action.handler==(uintptr_t)handle_masked_usr1 && native.guard==0xabcdef9876543210ULL);
 CHECK(native.action.mask & (1ULL << SIGUSR2));
 CHECK(sigaction(SIGUSR1,&old1,NULL)==0 && sigaction(SIGUSR2,&old2,NULL)==0);
 CHECK(sigaction(SIGUSR1,NULL,NULL)==0);
 struct sigaction default_action={.sa_handler=SIG_DFL};
 CHECK(sigaction(SIGKILL,&default_action,NULL)==-1 && errno==EINVAL);
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  struct sigaction once={.sa_handler=handle_usr2,.sa_flags=SA_RESETHAND};
  sigemptyset(&once.sa_mask);
  int before=usr2_count;
  if(sigaction(SIGUSR2,&once,NULL) || raise(SIGUSR2) || usr2_count!=before+1) _exit(108);
  struct sigaction current;
  if(sigaction(SIGUSR2,NULL,&current) || current.sa_handler!=SIG_DFL) _exit(109);
  raise(SIGUSR2); _exit(110);
 }
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGUSR2);
 return 0;
}
static volatile sig_atomic_t nested_depth, nested_peak, nested_calls, nested_bad;
static pid_t nested_pid;
static void handle_nested(int signo)
{
 int depth=++nested_depth;
 if(depth>nested_peak) nested_peak=depth;
 nested_calls++;
 sigset_t blocked;
 if(signo!=SIGUSR1 || sigprocmask(SIG_SETMASK,NULL,&blocked) ||
     sigismember(&blocked,SIGUSR1)) nested_bad=1;
 uint64_t marker=0x1234567800000000ULL+(unsigned)depth, observed;
 volatile uint64_t stack_cookie=marker;
 __asm__ volatile("movq %0, %%xmm15" : : "r"(marker) : "xmm15");
 if(depth<4 && nested_calls<10) {
  long number=SYSCALL_KILL;
  __asm__ volatile("int $0x80" : "+a"(number) : "D"((long)nested_pid),
      "S"((long)SIGUSR1) : "memory","cc","rcx","r11");
  if(number) nested_bad=1;
 }
 __asm__ volatile("movq %%xmm15, %0" : "=r"(observed));
 if(observed!=marker || stack_cookie!=marker) nested_bad=1;
 nested_depth--;
}
static volatile sig_atomic_t signal_child, signal_child_pid;
static void handle_forking_signal(int signo)
{
 (void)signo;
 pid_t child=fork();
 if(!child) signal_child=1;
 else signal_child_pid=child;
}
static void handle_overflow_signal(int signo)
{
 (void)signo;
 kill(getpid(),SIGUSR1);
}
static int nested_signal_check(void)
{
 nested_pid=getpid();
 struct sigaction action={.sa_handler=handle_nested,.sa_flags=SA_NODEFER}, previous;
 sigemptyset(&action.sa_mask);
 CHECK(sigaction(SIGUSR1,&action,&previous)==0);
 long number=SYSCALL_KILL;
 unsigned char near_ok,far_ok;
 uint64_t cookie=0xdeadbeefabcdef01ULL;
 /* Signals must preserve the entire SysV red zone, even though the port's
  * C code normally uses -mno-red-zone. */
 __asm__ volatile("movq %[cookie], -8(%%rsp); movq %[cookie], -128(%%rsp);"
     "int $0x80; cmpq %[cookie], -8(%%rsp); sete %[near_ok];"
     "cmpq %[cookie], -128(%%rsp); sete %[far_ok]"
     : "+a"(number), [near_ok]"=&q"(near_ok), [far_ok]"=&q"(far_ok)
     : "D"((long)nested_pid), "S"((long)SIGUSR1), [cookie]"r"(cookie)
     : "memory","cc","rcx","r11");
 CHECK(number==0 && near_ok && far_ok);
 CHECK(nested_depth==0 && nested_peak==4 && nested_calls==4 && !nested_bad);
 action.sa_handler=handle_forking_signal; action.sa_flags=0;
 CHECK(sigaction(SIGUSR1,&action,NULL)==0 && raise(SIGUSR1)==0);
 if(signal_child) _exit(0);
 CHECK(signal_child_pid>0);
 int status;
 CHECK(waitpid(signal_child_pid,&status,0)==signal_child_pid &&
     WIFEXITED(status) && !WEXITSTATUS(status));
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  action.sa_handler=handle_overflow_signal; action.sa_flags=SA_NODEFER;
  if(sigaction(SIGUSR1,&action,NULL)) _exit(111);
  raise(SIGUSR1); _exit(112);
 }
 CHECK(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGSEGV);
 CHECK(sigaction(SIGUSR1,&previous,NULL)==0);
 return 0;
}
static int shell_path_check(void)
{
 const char *cases[][2]={
  {"PATH=/missing:/sbin","init --shell-path-check"},
  {"PATH=/missing","/sbin/init --shell-path-check"},
  {"PATH=","./sbin/init --shell-path-check"}
 };
 for(size_t i=0;i<sizeof cases/sizeof cases[0];i++) {
  int channel[2]; CHECK(pipe(channel)==0);
  pid_t child=fork(); CHECK(child>=0);
  if(!child) {
   close(channel[0]);
   if(dup2(channel[1],STDOUT_FILENO)<0) _exit(120);
   close(channel[1]);
   char *args[]={"sh","-c",(char *)cases[i][1],NULL};
   char *env[]={(char *)cases[i][0],"MUSL_SHELL_CHECK=present",NULL};
   execve("/bin/sh",args,env);
   _exit(121);
  }
  CHECK(close(channel[1])==0);
  char output[128]; size_t used=0; ssize_t amount;
  while(used<sizeof output-1 && (amount=read(channel[0],output+used,sizeof output-1-used))>0)
   used+=(size_t)amount;
  output[used]=0;
  CHECK(close(channel[0])==0);
  int status;
  CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
  CHECK(!strcmp(output,"SHELL_PATH_OK\n"));
 }
 return 0;
}
static int power_permissions_check(void)
{
 CHECK(os_power_control(0)==-1 && errno==EINVAL);
 CHECK(os_framebuffer_info((void *)1)==-1 && errno==EFAULT);
 CHECK(os_framebuffer_present_rect((void *)1)==-1 && errno==EFAULT);
 CHECK(__os_raw(SYSCALL_POWER_CONTROL,0,0,0,0,0,0)==-EINVAL);
 CHECK(__os_raw(SYSCALL_POWER_CONTROL,-1,0,0,0,0,0)==-EINVAL);
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  if(!isatty(0) || setgid(1000) || setuid(1000)) _exit(120);
  if(os_power_control(0)!=-1 || errno!=EPERM) _exit(123);
  /* An invalid request tests authorization without invoking firmware. */
  if(__os_raw(SYSCALL_POWER_CONTROL,0,0,0,0,0,0)!=-EPERM) _exit(121);
  if(close(0) || __os_raw(SYSCALL_POWER_CONTROL,0,0,0,0,0,0)!=-EPERM) _exit(122);
  _exit(0);
 }
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
 return 0;
}

static int module_file_check(void)
{
 const char *path="/boot/modules/module_file_probe.ko";
 const char *name="module_file_probe";
 CHECK(os_module_load("/missing-module.ko")==-1 && errno==ENOENT);
 CHECK(os_module_load(path)==0);
 CHECK(os_module_load(path)==-1 && errno==EEXIST);
 CHECK(os_module_unload(name)==0);
 CHECK(os_module_unload(name)==-1 && errno==ENOENT);
 CHECK(__os_raw(SYSCALL_KLDLOAD,1,0,0,0,0,0)==-EFAULT);
 CHECK(__os_raw(SYSCALL_KLDLOAD,(long)"/missing-module.ko",0,0,0,0,0)==-ENOENT);
 CHECK(__os_raw(SYSCALL_KLDLOAD,(long)"/boot/modules",0,0,0,0,0)==-EINVAL);
 int fd=open("/tmp/invalid-module.ko",O_CREAT|O_EXCL|O_RDWR,0600); CHECK(fd>=0);
 CHECK(__os_raw(SYSCALL_KLDLOAD,(long)"/tmp/invalid-module.ko",0,0,0,0,0)==-EINVAL);
 CHECK(write(fd,"not an ELF module",17)==17 && close(fd)==0);
 CHECK(__os_raw(SYSCALL_KLDLOAD,(long)"/tmp/invalid-module.ko",0,0,0,0,0)==-EINVAL);
 CHECK(unlink("/tmp/invalid-module.ko")==0);
 for(int attempt=0;attempt<3;attempt++) {
  CHECK(__os_raw(SYSCALL_KLDLOAD,(long)path,0,0,0,0,0)==0);
  CHECK(__os_raw(SYSCALL_KLDLOAD,(long)path,0,0,0,0,0)==-EEXIST);
  CHECK(__os_raw(SYSCALL_KLDUNLOAD,(long)name,0,0,0,0,0)==0);
  CHECK(__os_raw(SYSCALL_KLDUNLOAD,(long)name,0,0,0,0,0)==-ENOENT);
 }
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  if(setgid(1000) || setuid(1000)) _exit(120);
  if(os_module_load(path)!=-1 || errno!=EPERM ||
      os_module_unload(name)!=-1 || errno!=EPERM) _exit(122);
  if(__os_raw(SYSCALL_KLDLOAD,(long)path,0,0,0,0,0)!=-EPERM ||
      __os_raw(SYSCALL_KLDUNLOAD,(long)name,0,0,0,0,0)!=-EPERM) _exit(121);
  _exit(0);
 }
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
 return 0;
}

static int shared_mapping_check(void)
{
 CHECK(__os_raw(SYSCALL_SHM_CREATE,0,0,0,0,0,0)==-EINVAL);
 for(int attempt=0;attempt<3;attempt++) {
  long base=__os_raw(SYSCALL_SHM_CREATE,8193,0,0,0,0,0);
  CHECK(base>0);
  volatile unsigned char *shared=(void *)base;
  for(size_t i=0;i<8193;i++) CHECK(shared[i]==0);
  shared[0]=7; shared[4096]=8; shared[8192]=9;
  pid_t child=fork(); CHECK(child>=0);
  if(!child) {
   if(shared[0]!=7 || shared[4096]!=8 || shared[8192]!=9) _exit(120);
   shared[0]=17; shared[4096]=18; shared[8192]=19;
   if(attempt==1) {
    char address[32]; snprintf(address,sizeof address,"%lu",(unsigned long)base);
    char *args[]={"shm-exec","--shm-exec-check",address,NULL};
    execv("/sbin/init",args); _exit(121);
   }
   if(attempt==2 && (__os_raw(SYSCALL_SHM_DETACH,base,0,0,0,0,0) ||
       write(1,(const void *)base,1)!=-1 || errno!=EFAULT)) _exit(122);
   _exit(31);
  }
  int status;
  CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) &&
      WEXITSTATUS(status)==(attempt==1 ? 30 : 31));
  CHECK(shared[0]==17 && shared[4096]==18 && shared[8192]==19);
  CHECK(__os_raw(SYSCALL_SHM_DETACH,base,0,0,0,0,0)==0);
  CHECK(write(1,(const void *)base,1)==-1 && errno==EFAULT);
 }
 return 0;
}

static int exec_limits_check(void)
{
 char argument[OS_EXEC_ARG_BYTES_MAX+1], environment[OS_EXEC_ENV_BYTES_MAX+1];
 char *args[OS_EXEC_ARG_MAX+2], *env[OS_EXEC_ENV_MAX+2];
 args[0]="exec-limits"; args[1]="--exec-limits-check";
 size_t used=strlen(args[0])+strlen(args[1])+2;
 for(size_t i=2;i<OS_EXEC_ARG_MAX-1;i++) { args[i]=""; used++; }
 size_t argument_length=OS_EXEC_ARG_BYTES_MAX-used-1;
 memset(argument,'a',argument_length); argument[argument_length]=0;
 args[OS_EXEC_ARG_MAX-1]=argument;
 args[OS_EXEC_ARG_MAX]=NULL; args[OS_EXEC_ARG_MAX+1]=NULL;
 for(size_t i=0;i<OS_EXEC_ENV_MAX-1;i++) env[i]="K=";
 size_t environment_length=OS_EXEC_ENV_BYTES_MAX-3*(OS_EXEC_ENV_MAX-1)-1;
 memset(environment,'x',environment_length); memcpy(environment,"LONG=",5);
 environment[environment_length]=0;
 env[OS_EXEC_ENV_MAX-1]=environment;
 env[OS_EXEC_ENV_MAX]=NULL; env[OS_EXEC_ENV_MAX+1]=NULL;
 args[OS_EXEC_ARG_MAX]="";
 CHECK(__os_raw(SYSCALL_EXECVE,(long)"/sbin/init",(long)args,(long)env,0,0,0)==-E2BIG);
 args[OS_EXEC_ARG_MAX]=NULL;
 env[OS_EXEC_ENV_MAX]="EXTRA=x";
 CHECK(__os_raw(SYSCALL_EXECVE,(long)"/sbin/init",(long)args,(long)env,0,0,0)==-E2BIG);
 env[OS_EXEC_ENV_MAX]=NULL;
 argument[argument_length]='a'; argument[argument_length+1]=0;
 CHECK(__os_raw(SYSCALL_EXECVE,(long)"/sbin/init",(long)args,(long)env,0,0,0)==-E2BIG);
 argument[argument_length]=0;
 environment[environment_length]='x'; environment[environment_length+1]=0;
 CHECK(__os_raw(SYSCALL_EXECVE,(long)"/sbin/init",(long)args,(long)env,0,0,0)==-E2BIG);
 environment[environment_length]=0;
 pid_t child=fork(); CHECK(child>=0);
 if(!child) { execve("/sbin/init",args,env); _exit(124); }
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==28);
 return 0;
}

static int process_check(void)
{
	CHECK(kill(getpid(),0)==0);
 sigset_t set, old, current;
 sigemptyset(&set);
 sigaddset(&set,SIGUSR1);
 CHECK(sigprocmask(SIG_BLOCK,&set,&old)==0);
 CHECK(sigprocmask(SIG_SETMASK,NULL,&current)==0);
 CHECK(sigismember(&current,SIGUSR1)==1);
 CHECK(sigprocmask(SIG_SETMASK,&old,NULL)==0);
 CHECK(getuid()==0 && geteuid()==0 && getgid()==0 && getegid()==0);
 int channel[2];
 CHECK(pipe(channel)==0);
 pid_t parent=getpid(), child=fork();
 CHECK(child>=0);
 if(!child) {
  if(getpid()==parent || getppid()!=parent || tls!=456) _exit(80);
  tls=789;
  if(close(channel[0]) || write(channel[1],"F",1)!=1) _exit(81);
  _exit(143); /* Must remain a regular exit, not SIGTERM. */
 }
 CHECK(close(channel[1])==0);
 char value=0;
 CHECK(read(channel[0],&value,1)==1 && value=='F' && close(channel[0])==0);
 struct { int status; unsigned guard; } result={-1,0xabcdef01};
 CHECK(waitpid(child,&result.status,0)==child);
 CHECK(WIFEXITED(result.status) && WEXITSTATUS(result.status)==143);
 CHECK(result.guard==0xabcdef01 && tls==456);
 CHECK(waitpid(child,NULL,WNOHANG)==-1 && errno==ECHILD);
 child=fork();
 CHECK(child>=0);
 if(!child) { for(;;) { struct timespec t={1,0}; nanosleep(&t,NULL); } }
 CHECK(waitpid(child,&result.status,WNOHANG)==0);
 CHECK(kill(child,SIGTERM)==0 && waitpid(child,&result.status,0)==child);
 CHECK(WIFSIGNALED(result.status) && WTERMSIG(result.status)==SIGTERM);
 child=fork();
 CHECK(child>=0);
 if(!child) {
  char *args[]={"musl-exec-check","--exec-check",NULL};
  char *env[]={"MUSL_EXEC_CHECK=present",NULL};
  execve("/sbin/init",args,env);
  _exit(82);
 }
 CHECK(waitpid(child,(int *)1,0)==-1 && errno==EFAULT);
 CHECK(waitpid(child,&result.status,0)==child);
 CHECK(WIFEXITED(result.status) && WEXITSTATUS(result.status)==27);
 child=fork();
 CHECK(child>=0);
 if(!child) _exit(59);
 struct { int status; unsigned guard; } native={0,0xaabbccdd};
 CHECK(__os_raw(SYSCALL_WAITPID,child,(long)&native.status,0,0,0,0)==child);
 CHECK(WIFEXITED(native.status) && WEXITSTATUS(native.status)==59 && native.guard==0xaabbccdd);
 return 0;
}
static int shell_pty_output(int fd, const char *first, const char *second)
{
 char output[8192]; size_t used=0;
 output[0]=0;
 for(int i=0;i<200;i++) {
  char *found=strstr(output,first);
  if(found && strstr(found+strlen(first),second)) return 1;
  struct pollfd input={fd,POLLIN,0};
  int ready=poll(&input,1,50);
  if(ready<0 || (input.revents&(POLLERR|POLLNVAL))) return 0;
  if(!ready) continue;
  ssize_t amount=read(fd,output+used,sizeof output-1-used);
  if(amount<=0) return 0;
  used+=(size_t)amount; output[used]=0;
  if(used==sizeof output-1) return 0;
 }
 return 0;
}

static int shell_pty_check(void)
{
 int pair[2];
 CHECK(__os_raw(SYSCALL_PTY_PAIR,(long)pair,0,0,0,0,0)==0);
 struct termios original,actual;
 CHECK(tcgetattr(pair[1],&original)==0);
 original.c_lflag|=ICANON|ECHO;
 original.c_oflag=OPOST|ONLCR;
 original.c_cc[VERASE]=8;
 CHECK(tcsetattr(pair[1],TCSANOW,&original)==0);
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  close(pair[0]);
  if(dup2(pair[1],0)<0 || dup2(pair[1],1)<0 || dup2(pair[1],2)<0) _exit(120);
  close(pair[1]);
  char *args[]={"sh","--xterm",NULL};
  execv("/bin/sh",args); _exit(121);
 }
 CHECK(shell_pty_output(pair[0],"","% "));
 CHECK(tcgetattr(pair[1],&actual)==0 && !(actual.c_lflag&(ICANON|ECHO)) &&
     actual.c_oflag==original.c_oflag && actual.c_cc[VERASE]==8);
 const char cancel[]="discard\003";
 CHECK(write(pair[0],cancel,sizeof cancel-1)==sizeof cancel-1);
 CHECK(shell_pty_output(pair[0],"^C","% "));
 const char command[]="/sbin/init --shell-tty-check\n";
 CHECK(write(pair[0],command,sizeof command-1)==sizeof command-1);
 CHECK(shell_pty_output(pair[0],"SHELL_TTY_OK","% "));
 CHECK(tcgetattr(pair[1],&actual)==0 && !(actual.c_lflag&(ICANON|ECHO)));
 CHECK(write(pair[0],"history\n",8)==8);
 CHECK(shell_pty_output(pair[0],"1  /sbin/init --shell-tty-check","% "));
 CHECK(write(pair[0],"exit\n",5)==5);
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
 CHECK(tcgetattr(pair[1],&actual)==0 && actual.c_iflag==original.c_iflag &&
     actual.c_oflag==original.c_oflag && actual.c_cflag==original.c_cflag &&
     actual.c_lflag==original.c_lflag && !memcmp(actual.c_cc,original.c_cc,NCCS));
 CHECK(close(pair[0])==0 && close(pair[1])==0);
 return 0;
}

static int terminal_check(void)
{
 int pair[2];
 CHECK(__os_raw(SYSCALL_PTY_PAIR,(long)pair,0,0,0,0,0)==0);
 struct termios tty;
 CHECK(tcgetattr(pair[1],&tty)==0 && (tty.c_lflag&ICANON));
 CHECK(ioctl(pair[1],0x4f530001U,NULL)==-1 && errno==ENOTTY);
 CHECK(ioctl(pair[1],0x4f530002U,NULL)==-1 && errno==ENOTTY);
 CHECK(ioctl(0,0x4f530001U,NULL)==-1 && errno==ENOTTY);
 CHECK(ioctl(0,0x4f530002U,NULL)==-1 && errno==ENOTTY);
 CHECK(tty.c_cc[VINTR]==3 && tty.c_cc[VEOF]==4 && tty.c_cc[VERASE]==127);
 struct winsize size={.ws_row=37,.ws_col=91}, retrieved;
 CHECK(ioctl(pair[1],TIOCSWINSZ,&size)==0 && ioctl(pair[1],TIOCGWINSZ,&retrieved)==0);
 CHECK(retrieved.ws_row==37 && retrieved.ws_col==91);
 struct { struct os_winsize value; uint64_t guard; } native_window={.guard=0x12345678abcdef01ULL};
 CHECK(__os_raw(SYSCALL_IOCTL,pair[1],OS_TIOCGWINSZ,(long)&native_window.value,0,0,0)==0);
 CHECK(native_window.value.rows==37 && native_window.value.columns==91 &&
     native_window.guard==0x12345678abcdef01ULL);
 native_window.value=(struct os_winsize){42,96,800,600};
 CHECK(__os_raw(SYSCALL_IOCTL,pair[1],OS_TIOCSWINSZ,(long)&native_window.value,0,0,0)==0);
 CHECK(ioctl(pair[1],TIOCGWINSZ,&retrieved)==0 && retrieved.ws_row==42 &&
     retrieved.ws_col==96 && retrieved.ws_xpixel==800 && retrieved.ws_ypixel==600);
 CHECK(ioctl(pair[1],TIOCSWINSZ,(void *)1)==-1 && errno==EFAULT);
 CHECK(ioctl(pair[1],TIOCGWINSZ,(void *)1)==-1 && errno==EFAULT);
 CHECK(ioctl(pair[1],TIOCGWINSZ,&retrieved)==0 && retrieved.ws_row==42 && retrieved.ws_col==96);

 cfmakeraw(&tty);
 CHECK(tcsetattr(pair[1],TCSANOW,&tty)==0);
 CHECK(tcgetattr(pair[1],&tty)==0 && !(tty.c_lflag&ICANON) && !(tty.c_lflag&ECHO));
 CHECK(write(pair[0],"R",1)==1);
 int available=0;
 CHECK(ioctl(pair[1],FIONREAD,&available)==0 && available==1);
 char value;
 CHECK(read(pair[1],&value,1)==1 && value=='R');
 CHECK(isatty(pair[1]));
 CHECK(write(pair[0],"pending",7)==7);
 CHECK(tcsetattr(pair[1],TCSADRAIN,&tty)==0);
 CHECK(ioctl(pair[1],FIONREAD,&available)==0 && available==7);
 CHECK(tcsetattr(pair[1],TCSAFLUSH,&tty)==0);
 CHECK(ioctl(pair[1],FIONREAD,&available)==0 && available==0);
 CHECK(write(pair[0],"discard",7)==7 && tcflush(pair[1],TCIFLUSH)==0);
 CHECK(ioctl(pair[1],FIONREAD,&available)==0 && available==0);
 CHECK(tcflush(pair[1],99)==-1 && errno==EINVAL);
 CHECK(write(pair[1],"output",6)==6 && tcdrain(pair[1])==0);
 char output[8]={0};
 CHECK(read(pair[0],output,6)==6 && !strcmp(output,"output"));
 tty.c_cc[VERASE]='x'; tty.c_cc[VEOF]='z';
 tty.c_lflag|=ICANON; tty.c_lflag&=~ECHO;
 CHECK(cfsetospeed(&tty,B9600)==0 && tcsetattr(pair[1],TCSANOW,&tty)==0);
 struct termios roundtrip;
 CHECK(tcgetattr(pair[1],&roundtrip)==0 && cfgetospeed(&roundtrip)==B9600 &&
     roundtrip.c_cc[VERASE]=='x' && roundtrip.c_cc[VEOF]=='z');
 CHECK(write(pair[0],"abxc\n",5)==5);
 memset(output,0,sizeof output);
 CHECK(read(pair[1],output,sizeof output)==3 && !strcmp(output,"ac\n"));
 CHECK(write(pair[0],"z",1)==1 && read(pair[1],output,sizeof output)==0);
 CHECK(close(pair[0])==0 && close(pair[1])==0);
 return 0;
}
static int curses_check(void)
{
 CHECK(setenv("TERM","xterm-256color",1)==0);
 SCREEN *screen=newterm(NULL,stdout,stdin);
 CHECK(screen);
 CHECK(cbreak()==OK && noecho()==OK);
 CHECK(mvaddstr(1,2,"musl ncurses check")==OK && refresh()==OK);
 CHECK(endwin()==OK);
 delscreen(screen);
 return 0;
}
static int poll_check(void)
{
 int channel[2]; CHECK(pipe(channel)==0);
 struct pollfd fds[3]={{channel[0],POLLIN,0},{-1,POLLIN,99},{12345,0,0}};
 CHECK(poll(fds,3,1000)==1 && fds[2].revents==POLLNVAL && !fds[1].revents);
 CHECK(poll(fds,1,0)==0 && !fds[0].revents);
 CHECK(write(channel[1],"P",1)==1);
 CHECK(poll(fds,1,1000)==1 && (fds[0].revents&POLLIN));
 CHECK(close(channel[1])==0);
 CHECK(poll(fds,1,0)==1 && (fds[0].revents&POLLHUP) && (fds[0].revents&POLLIN));
 char value; CHECK(read(channel[0],&value,1)==1 && value=='P');
 fds[0].events=0;
 CHECK(poll(fds,1,0)==1 && fds[0].revents==POLLHUP);
 CHECK(close(channel[0])==0);
 CHECK(syscall(SYS_poll,1,1,0)==-1 && errno==EFAULT);
 CHECK(poll(NULL,OS_POLL_MAX+1,0)==-1 && errno==EINVAL);
 struct timespec before,after;
 CHECK(clock_gettime(CLOCK_MONOTONIC,&before)==0 && poll(NULL,0,30)==0);
 CHECK(clock_gettime(CLOCK_MONOTONIC,&after)==0);
 CHECK((after.tv_sec-before.tv_sec)*1000000000L+after.tv_nsec-before.tv_nsec>=20000000);
 struct sigaction action={.sa_handler=handle_usr1}, previous;
 sigemptyset(&action.sa_mask);
 CHECK(sigaction(SIGUSR1,&action,&previous)==0);
 pid_t parent=getpid(), child=fork(); CHECK(child>=0);
 if(!child) {
  struct timespec delay={0,50000000}; nanosleep(&delay,NULL);
  _exit(kill(parent,SIGUSR1)==0 ? 0 : 94);
 }
 CHECK(poll(NULL,0,-1)==-1 && errno==EINTR);
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
 CHECK(sigaction(SIGUSR1,&previous,NULL)==0);
 return 0;
}
static int dns_check(void)
{
 struct resolvconf conf;
 unsigned char dhcp_dns[4];
 size_t length=sizeof dhcp_dns;
 CHECK(sysctlbyname("net.inet.ip.dns",dhcp_dns,&length,NULL,0)==0 && length==sizeof dhcp_dns);
 CHECK(__get_resolv_conf(&conf,NULL,0)==0 && conf.nns==1 &&
     conf.ns[0].family==AF_INET && !memcmp(conf.ns[0].addr,dhcp_dns,4));
 unsigned char config[12];
 length=sizeof config;
 CHECK(sysctlbyname("net.inet.ip.config",config,&length,NULL,0)==0 && length==sizeof config);
 struct { struct os_ipv4_config value; uint32_t guard; } snapshot={.guard=0xabcdef01U};
 length=sizeof snapshot.value;
 CHECK(sysctlbyname("net.inet.ip.config",&snapshot.value,&length,NULL,0)==0 &&
     length==sizeof snapshot.value && !memcmp(&snapshot.value,config,sizeof config) &&
     snapshot.guard==0xabcdef01U);
 struct { unsigned char bytes[3]; unsigned char guard; } prefix={.guard=0xb7};
 length=sizeof prefix.bytes;
 CHECK(sysctlbyname("net.inet.ip.dns",prefix.bytes,&length,NULL,0)==-1 && errno==ENOMEM);
 CHECK(length==sizeof prefix.bytes && !memcmp(prefix.bytes,dhcp_dns,sizeof prefix.bytes) && prefix.guard==0xb7);
 char interface[32]; length=sizeof interface;
 CHECK(sysctlbyname("net.interface",interface,&length,NULL,0)==0 &&
     length>1 && length<=sizeof interface && interface[length-1]==0);

 int server=socket(AF_INET,SOCK_DGRAM,0);
 CHECK(server>=0);
 struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(53)};
 memcpy(&address.sin_addr,config,4);
 CHECK(bind(server,(struct sockaddr *)&address,sizeof address)==0);
 FILE *resolv=fopen("/etc/resolv.conf","w"); CHECK(resolv);
 CHECK(fprintf(resolv,"nameserver %u.%u.%u.%u\noptions timeout:1 attempts:1\n",config[0],config[1],config[2],config[3])>0);
 CHECK(fclose(resolv)==0);
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  unsigned char packet[512]; struct sockaddr_in peer; socklen_t size=sizeof peer;
  int received=recvfrom(server,packet,sizeof packet,0,(struct sockaddr *)&peer,&size);
  if(received<17 || received>480) _exit(95);
  size_t end=12;
  while(end<(size_t)received && packet[end]) end+=packet[end]+1;
  end+=5;
  if(end>(size_t)received || end+16>sizeof packet) _exit(96);
  packet[2]=0x81; packet[3]=0x80; packet[6]=0; packet[7]=1;
  packet[8]=packet[9]=packet[10]=packet[11]=0;
  const unsigned char answer[]={0xc0,0x0c,0,1,0,1,0,0,0,1,0,4,192,0,2,42};
  memcpy(packet+end,answer,sizeof answer);
  int sent=sendto(server,packet,end+sizeof answer,0,(struct sockaddr *)&peer,size);
  close(server); _exit(sent==(int)(end+sizeof answer) ? 0 : 97);
 }
 CHECK(close(server)==0);
 struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM}, *result=NULL;
 int resolved=getaddrinfo("musl-port.test","80",&hints,&result);
 if(resolved) fprintf(stderr,"DNS result %d: %s\n",resolved,gai_strerror(resolved));
 CHECK(resolved==0 && result && result->ai_family==AF_INET);
 struct sockaddr_in *in=(void *)result->ai_addr;
 const unsigned char expected[]={192,0,2,42};
 CHECK(!memcmp(&in->sin_addr,expected,4) && ntohs(in->sin_port)==80);
 freeaddrinfo(result);
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
 CHECK(unlink("/etc/resolv.conf")==0);
 CHECK(__get_resolv_conf(&conf,NULL,0)==0 && !memcmp(conf.ns[0].addr,dhcp_dns,4));
 return 0;
}
static int flock_check(void)
{
 int first=open("/tmp/flock-check",O_CREAT|O_RDWR,0600);
 int second=open("/tmp/flock-check",O_RDWR);
 CHECK(first>=0 && second>=0);
 CHECK(flock(first,LOCK_SH)==0 && flock(second,LOCK_SH|LOCK_NB)==0);
 CHECK(flock(first,LOCK_EX|LOCK_NB)==-1 && errno==EAGAIN);
 CHECK(flock(second,LOCK_EX|LOCK_NB)==0);
 CHECK(flock(first,LOCK_SH|LOCK_NB)==-1 && errno==EAGAIN);
 CHECK(write(first,"advisory",8)==8);
 CHECK(flock(second,LOCK_UN)==0 && flock(first,LOCK_EX)==0);
 int duplicate=dup(first);
 CHECK(duplicate>=0 && close(first)==0);
 CHECK(flock(second,LOCK_EX|LOCK_NB)==-1 && errno==EAGAIN);
 CHECK(close(duplicate)==0 && flock(second,LOCK_EX|LOCK_NB)==0);
 CHECK(flock(second,LOCK_UN)==0);
 CHECK(flock(-1,LOCK_EX)==-1 && errno==EBADF);
 CHECK(flock(second,LOCK_SH|LOCK_EX)==-1 && errno==EINVAL);
 CHECK(flock(second,LOCK_NB)==-1 && errno==EINVAL);
 CHECK(__os_raw(SYSCALL_FLOCK,second,0x100000002L,0,0,0,0)==-EINVAL);
 int channel[2]; CHECK(pipe(channel)==0);
 CHECK(flock(channel[0],LOCK_EX)==-1 && errno==EOPNOTSUPP);
 CHECK(close(channel[0])==0 && close(channel[1])==0);
 CHECK(pipe(channel)==0);
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  close(channel[0]);
  int held=open("/tmp/flock-check",O_RDWR);
  if(held<0 || flock(held,LOCK_EX) || write(channel[1],"L",1)!=1) _exit(91);
  close(channel[1]);
  struct timespec delay={0,150000000};
  nanosleep(&delay,NULL);
  _exit(0);
 }
 CHECK(close(channel[1])==0);
 char ready;
 CHECK(read(channel[0],&ready,1)==1 && ready=='L' && close(channel[0])==0);
 CHECK(flock(second,LOCK_SH|LOCK_NB)==-1 && errno==EAGAIN);
 CHECK(flock(second,LOCK_EX)==0);
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
 CHECK(close(second)==0 && unlink("/tmp/flock-check")==0);
 int directory=open("/tmp",O_RDONLY|O_DIRECTORY);
 int other=open("/tmp",O_RDONLY|O_DIRECTORY);
 CHECK(directory>=0 && other>=0 && flock(directory,LOCK_EX)==0);
 CHECK(flock(other,LOCK_EX|LOCK_NB)==-1 && errno==EAGAIN);
 CHECK(close(directory)==0 && flock(other,LOCK_EX|LOCK_NB)==0 && close(other)==0);
 return 0;
}
static int network_check(void)
{
 const long socket_cases[][4]={
  {OS_AF_UNIX,OS_SOCK_STREAM,0,OS_SOCKET_KIND_UNIX},
  {OS_AF_INET,OS_SOCK_STREAM,0,OS_SOCKET_KIND_TCP},
  {OS_AF_INET,OS_SOCK_STREAM,OS_IPPROTO_TCP,OS_SOCKET_KIND_TCP},
  {OS_AF_INET,OS_SOCK_DGRAM,0,OS_SOCKET_KIND_UDP},
  {OS_AF_INET,OS_SOCK_DGRAM,OS_IPPROTO_UDP,OS_SOCKET_KIND_UDP}
 };
 for(size_t i=0;i<sizeof socket_cases/sizeof socket_cases[0];i++) {
  const long *item=socket_cases[i];
  long fd=__os_raw(SYSCALL_SOCKET,item[0],item[1],item[2],0,0,0);
  CHECK(fd>=0 && __os_raw(SYSCALL_SOCKET_KIND,fd,0,0,0,0,0)==item[3]);
  CHECK(close(fd)==0);
 }
 const long invalid_sockets[][4]={
  {0,OS_SOCK_STREAM,0,EAFNOSUPPORT},
  {0x100000001L,OS_SOCK_STREAM,0,EAFNOSUPPORT},
  {OS_AF_UNIX,0x100000001L,0,EPROTONOSUPPORT},
  {OS_AF_UNIX,OS_SOCK_DGRAM,0,EPROTONOSUPPORT},
  {OS_AF_UNIX,OS_SOCK_STREAM,OS_IPPROTO_TCP,EPROTONOSUPPORT},
  {OS_AF_INET,0,0,EPROTONOSUPPORT},
  {OS_AF_INET,OS_SOCK_STREAM,OS_IPPROTO_UDP,EPROTONOSUPPORT},
  {OS_AF_INET,OS_SOCK_DGRAM,OS_IPPROTO_TCP,EPROTONOSUPPORT}
 };
 for(size_t i=0;i<sizeof invalid_sockets/sizeof invalid_sockets[0];i++) {
  const long *item=invalid_sockets[i];
  CHECK(__os_raw(SYSCALL_SOCKET,item[0],item[1],item[2],0,0,0)==-item[3]);
 }
 const long address_calls[]={SYSCALL_BIND,SYSCALL_CONNECT};
 int sockets[]={socket(AF_UNIX,SOCK_STREAM,0),socket(AF_INET,SOCK_STREAM,0),
     socket(AF_INET,SOCK_DGRAM,0)};
 int channel[2]; CHECK(pipe(channel)==0);
 for(size_t call=0;call<sizeof address_calls/sizeof address_calls[0];call++) {
  CHECK(__os_raw(address_calls[call],12345,0,0,0,0,0)==-EBADF);
  CHECK(__os_raw(address_calls[call],channel[0],0,0,0,0,0)==-ENOTSOCK);
  for(size_t i=0;i<sizeof sockets/sizeof sockets[0];i++) {
   CHECK(sockets[i]>=0);
   size_t size=i==0 ? sizeof(struct sockaddr_un) : sizeof(struct os_ipv4_endpoint);
   CHECK(__os_raw(address_calls[call],sockets[i],1,size,0,0,0)==-EFAULT);
   CHECK(__os_raw(address_calls[call],sockets[i],1,0,0,0,0)==-EINVAL);
   CHECK(__os_raw(address_calls[call],sockets[i],1,-1,0,0,0)==-EINVAL);
  }
 }
 for(size_t i=0;i<sizeof sockets/sizeof sockets[0];i++) CHECK(close(sockets[i])==0);
 CHECK(close(channel[0])==0 && close(channel[1])==0);
 int name_listener=socket(AF_UNIX,SOCK_STREAM,0);
 CHECK(name_listener>=0);
 struct sockaddr_un listener_name={.sun_family=AF_UNIX,.sun_path="/musl-name-listener"};
 socklen_t listener_name_length=offsetof(struct sockaddr_un,sun_path)+strlen(listener_name.sun_path)+1;
 struct sockaddr_un unsupported_name={.sun_family=AF_UNIX};
 memcpy(unsupported_name.sun_path,"\0A\0B",4);
 socklen_t unsupported_length=offsetof(struct sockaddr_un,sun_path)+4;
 CHECK(bind(name_listener,(struct sockaddr *)&unsupported_name,unsupported_length)==-1 && errno==EINVAL);
 CHECK(connect(name_listener,(struct sockaddr *)&unsupported_name,unsupported_length)==-1 && errno==EINVAL);
 struct sockaddr_un queried;
 socklen_t queried_length=sizeof queried;
 memset(&queried,0xa5,sizeof queried);
 CHECK(getsockname(name_listener,(struct sockaddr *)&queried,&queried_length)==0 &&
     queried_length==sizeof queried.sun_family && queried.sun_family==AF_UNIX &&
     (unsigned char)queried.sun_path[0]==0xa5);
 CHECK(getpeername(name_listener,(struct sockaddr *)&queried,&queried_length)==-1 && errno==ENOTCONN);
 queried_length=listener_name_length;
 CHECK(bind(name_listener,(struct sockaddr *)&listener_name,queried_length)==0);
 CHECK(getsockname(name_listener,(struct sockaddr *)&queried,&queried_length)==0 &&
     queried_length==listener_name_length &&
     !memcmp(&queried,&listener_name,queried_length));
 CHECK(listen(name_listener,1)==0);
 int named_client=socket(AF_UNIX,SOCK_STREAM,0);
 CHECK(named_client>=0);
 struct sockaddr_un client_name={.sun_family=AF_UNIX,.sun_path="/musl-peer-name"};
 socklen_t client_name_length=offsetof(struct sockaddr_un,sun_path)+strlen(client_name.sun_path)+1;
 CHECK(bind(named_client,(struct sockaddr *)&client_name,sizeof client_name)==0);
 CHECK(connect(named_client,(struct sockaddr *)&listener_name,queried_length)==0);
 int accepted=accept(name_listener,NULL,NULL);
 CHECK(accepted>=0 && close(name_listener)==0);
 queried_length=sizeof queried;
 CHECK(getpeername(named_client,(struct sockaddr *)&queried,&queried_length)==0 &&
     queried_length==listener_name_length &&
     !memcmp(&queried,&listener_name,queried_length));
 queried_length=sizeof queried;
 CHECK(getsockname(accepted,(struct sockaddr *)&queried,&queried_length)==0 &&
     queried_length==listener_name_length &&
     !memcmp(&queried,&listener_name,queried_length));
 CHECK(close(named_client)==0);
 queried_length=sizeof queried;
 CHECK(getpeername(accepted,(struct sockaddr *)&queried,&queried_length)==0 &&
     queried_length==client_name_length && !strcmp(queried.sun_path,client_name.sun_path));
 CHECK(close(accepted)==0 && unlink(client_name.sun_path)==0 && unlink(listener_name.sun_path)==0);
 struct sockaddr_un address={.sun_family=AF_UNIX,.sun_path="/musl-socket"};
 int server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
 CHECK(server>=0 && (fcntl(server,F_GETFD)&FD_CLOEXEC));
 CHECK(fcntl(server,F_GETFL)&O_NONBLOCK);
 CHECK(bind(server,(struct sockaddr *)&address,sizeof address)==0 && listen(server,2)==0);
 queried_length=sizeof queried;
 socklen_t server_name_length=offsetof(struct sockaddr_un,sun_path)+strlen(address.sun_path)+1;
 CHECK(getsockname(server,(struct sockaddr *)&queried,&queried_length)==0 &&
     queried_length==server_name_length && !strcmp(queried.sun_path,address.sun_path));
 struct { unsigned char bytes[3]; unsigned char guard; } short_name={.guard=0xb7};
 queried_length=sizeof short_name.bytes;
 CHECK(getsockname(server,(struct sockaddr *)short_name.bytes,&queried_length)==0 &&
     queried_length==server_name_length && short_name.guard==0xb7 &&
     !memcmp(short_name.bytes,&address,sizeof short_name.bytes));
 CHECK(__os_raw(SYSCALL_GETSOCKNAME,server,0,0,0,0,0)==server_name_length);
 CHECK(accept(server,NULL,NULL)==-1 && errno==EAGAIN);
 int type=0; socklen_t length=sizeof type;
 CHECK(getsockopt(server,SOL_SOCKET,SO_TYPE,&type,&length)==0 && type==SOCK_STREAM);
 int duplicate=dup(server);
 CHECK(duplicate>=0);
 length=sizeof type;
 CHECK(getsockopt(duplicate,SOL_SOCKET,SO_DOMAIN,&type,&length)==0 && type==AF_UNIX);
 CHECK(close(duplicate)==0);
 pid_t child=fork();
 CHECK(child>=0);
 if(!child) {
  close(server);
  int client=socket(AF_UNIX,SOCK_STREAM,0);
  if(client<0 || connect(client,(struct sockaddr *)&address,sizeof address)) _exit(90);
  queried_length=sizeof queried;
  if(getpeername(client,(struct sockaddr *)&queried,&queried_length) ||
      queried_length!=server_name_length || strcmp(queried.sun_path,address.sun_path)) _exit(96);
  queried_length=sizeof queried;
  if(getsockname(client,(struct sockaddr *)&queried,&queried_length) ||
      queried_length!=sizeof queried.sun_family || queried.sun_family!=AF_UNIX) _exit(97);
  if(send(client,"hello",5,0)!=5) _exit(91);
  char reply[3]={0};
  if(recv(client,reply,2,0)!=2 || strcmp(reply,"ok")) _exit(92);
  unsigned char large[8192]; size_t used=0;
  while(used<sizeof large) {
   struct iovec input[2]={{NULL,0},{large+used,sizeof large-used}};
   struct msghdr header={.msg_iov=input,.msg_iovlen=2};
   ssize_t got=recvmsg(client,&header,0);
   if(got<=0 || (size_t)got>sizeof large-used || header.msg_flags || header.msg_controllen) _exit(93);
   used+=(size_t)got;
  }
  for(size_t i=0;i<sizeof large;i++) if(large[i]!=(unsigned char)(i*13+7)) _exit(94);
  struct iovec output[2]={{large,19},{large+19,sizeof large-19}};
  struct msghdr header={.msg_iov=output,.msg_iovlen=2};
  if(sendmsg(client,&header,0)!=sizeof large) _exit(95);
  used=0;
  while(used<sizeof large) {
   ssize_t got=recv(client,large+used,sizeof large-used,0);
   if(got<=0 || (size_t)got>sizeof large-used) _exit(98);
   used+=(size_t)got;
  }
  if(send(client,large,sizeof large,0)!=sizeof large) _exit(99);
  close(client);
  _exit(0);
 }
 fd_set reads;
 FD_ZERO(&reads); FD_SET(server,&reads);
 struct timeval timeout={2,0};
 CHECK(select(server+1,&reads,NULL,NULL,&timeout)==1 && FD_ISSET(server,&reads));
 int peer=accept4(server,NULL,NULL,SOCK_CLOEXEC);
 CHECK(peer>=0 && (fcntl(peer,F_GETFD)&FD_CLOEXEC));
 queried_length=sizeof queried;
 CHECK(getsockname(peer,(struct sockaddr *)&queried,&queried_length)==0 &&
     queried_length==server_name_length && !strcmp(queried.sun_path,address.sun_path));
 queried_length=sizeof queried;
 CHECK(getpeername(peer,(struct sockaddr *)&queried,&queried_length)==0 &&
     queried_length==sizeof queried.sun_family && queried.sun_family==AF_UNIX);
 FD_ZERO(&reads); FD_SET(peer,&reads);
 timeout=(struct timeval){2,0};
 CHECK(select(peer+1,&reads,NULL,NULL,&timeout)==1);
 fd_set writes;
 FD_ZERO(&reads); FD_SET(peer,&reads);
 FD_ZERO(&writes); FD_SET(peer,&writes);
 timeout=(struct timeval){0};
 CHECK(select(peer+1,&reads,&writes,NULL,&timeout)==2);
 char message[6]={0};
 CHECK(recv(peer,message,5,0)==5 && !strcmp(message,"hello"));
 CHECK(send(peer,"ok",2,0)==2);
 unsigned char large_message[8192], large_reply[8192];
 for(size_t i=0;i<sizeof large_message;i++) large_message[i]=(unsigned char)(i*13+7);
 struct iovec stream_vectors[2]={{large_message,23},{large_message+23,sizeof large_message-23}};
 struct msghdr stream_header={.msg_iov=stream_vectors,.msg_iovlen=2};
 CHECK(sendmsg(peer,&stream_header,0)==sizeof large_message);
 int status;
 stream_vectors[0]=(struct iovec){large_reply,31};
 stream_vectors[1]=(struct iovec){large_reply+31,sizeof large_reply-31};
 stream_header.msg_flags=123;
 CHECK(recvmsg(peer,&stream_header,0)==sizeof large_reply && stream_header.msg_flags==0 &&
     stream_header.msg_controllen==0 && !memcmp(large_message,large_reply,sizeof large_reply));
 CHECK(send(peer,large_message,sizeof large_message,0)==sizeof large_message);
 size_t scalar_received=0;
 while(scalar_received<sizeof large_reply) {
  ssize_t got=recv(peer,large_reply+scalar_received,sizeof large_reply-scalar_received,0);
  CHECK(got>0 && (size_t)got<=sizeof large_reply-scalar_received);
  scalar_received+=(size_t)got;
 }
 CHECK(!memcmp(large_message,large_reply,sizeof large_reply));
 CHECK(recv(peer,message,sizeof message,0)==0);
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
 CHECK(shutdown(peer,SHUT_RDWR)==0 && close(peer)==0 && close(server)==0);
 CHECK(unlink(address.sun_path)==0);
 unsigned char config[12];
 int configured=0;
 for(int i=0;i<100;i++) {
  size_t size=sizeof config;
  if(sysctlbyname("net.inet.ip.config",config,&size,NULL,0)==0) {configured=1;break;}
  struct timespec pause={0,100000000}; nanosleep(&pause,NULL);
 }
 CHECK(configured);
 struct { struct os_icmp_echo_request value; uint64_t guard; } echo={.guard=0xabcd0123456789efULL};
 memcpy(echo.value.destination,config+8,4);
 echo.value.identifier=(uint16_t)getpid(); echo.value.sequence=7;
 echo.value.payload_length=OS_ICMP_ECHO_PAYLOAD_MAX;
 for(size_t i=0;i<sizeof echo.value.payload;i++) echo.value.payload[i]=(unsigned char)(i*13+5);
 CHECK(__os_raw(SYSCALL_ICMP_ECHO,(long)&echo.value,0,sizeof echo.value-1,0,0,0)==-EINVAL);
 CHECK(__os_raw(SYSCALL_ICMP_ECHO,1,0,sizeof echo.value,0,0,0)==-EFAULT);
 echo.value.reserved=1;
 CHECK(__os_raw(SYSCALL_ICMP_ECHO,(long)&echo.value,0,sizeof echo.value,0,0,0)==-EINVAL);
 echo.value.reserved=0; echo.value.payload_length=OS_ICMP_ECHO_PAYLOAD_MAX+1;
 CHECK(__os_raw(SYSCALL_ICMP_ECHO,(long)&echo.value,0,sizeof echo.value,0,0,0)==-EINVAL);
 echo.value.payload_length=OS_ICMP_ECHO_PAYLOAD_MAX;
 struct timespec echo_before,echo_after;
 CHECK(clock_gettime(CLOCK_MONOTONIC,&echo_before)==0);
 CHECK(__os_raw(SYSCALL_ICMP_ECHO,(long)&echo.value,0,sizeof echo.value,0,0,0)==0);
 CHECK(clock_gettime(CLOCK_MONOTONIC,&echo_after)==0);
 uint64_t elapsed=(echo_after.tv_sec-echo_before.tv_sec)*1000000000ULL+
     echo_after.tv_nsec-echo_before.tv_nsec;
 CHECK(!memcmp(echo.value.destination,echo.value.reply_source,4) && echo.value.elapsed_ns>0 &&
     echo.value.elapsed_ns<=elapsed+10000000 && echo.guard==0xabcd0123456789efULL);
 CHECK(os_icmp_echo(NULL)==-1 && errno==EFAULT);
 echo.value.reserved=1;
 CHECK(os_icmp_echo(&echo.value)==-1 && errno==EINVAL);
 echo.value.reserved=0; echo.value.sequence++; echo.value.payload_length=0;
 CHECK(os_icmp_echo(&echo.value)==0 && echo.value.elapsed_ns>0 &&
     !memcmp(echo.value.destination,echo.value.reply_source,4));


 int udp=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);
 CHECK(udp>=0);
 struct sockaddr_in local={.sin_family=AF_INET};
 CHECK(bind(udp,(struct sockaddr *)&local,sizeof local)==0);
 length=sizeof local;
 CHECK(getsockname(udp,(struct sockaddr *)&local,&length)==0 && length==sizeof local && local.sin_port!=0);
 struct { struct os_ipv4_endpoint value; uint16_t guard; } native_endpoint={.guard=0xabcd};
 CHECK(__os_raw(SYSCALL_GETSOCKNAME,udp,(long)&native_endpoint.value,sizeof native_endpoint.value,0,0,0)==sizeof native_endpoint.value);
 CHECK(native_endpoint.value.port==ntohs(local.sin_port) &&
     !memcmp(native_endpoint.value.address,&local.sin_addr,4) && native_endpoint.guard==0xabcd);
 struct os_ipv4_endpoint unchanged_endpoint=native_endpoint.value;
 const long name_calls[]={SYSCALL_GETSOCKNAME,SYSCALL_GETPEERNAME};
 for(size_t i=0;i<sizeof name_calls/sizeof name_calls[0];i++) {
  CHECK(__os_raw(name_calls[i],-1,1,0,0,0,0)==-EBADF);
  CHECK(__os_raw(name_calls[i],0,1,0,0,0,0)==-ENOTSOCK);
 }
 CHECK(__os_raw(SYSCALL_GETSOCKNAME,udp,1,sizeof native_endpoint.value,0,0,0)==-EFAULT);
 CHECK(__os_raw(SYSCALL_GETSOCKNAME,udp,0,0,0,0,0)==sizeof native_endpoint.value);
 struct { unsigned char bytes[3]; unsigned char guard; } prefix={.guard=0xa7};
 CHECK(__os_raw(SYSCALL_GETSOCKNAME,udp,(long)prefix.bytes,sizeof prefix.bytes,0,0,0)==sizeof native_endpoint.value);
 CHECK(!memcmp(prefix.bytes,&native_endpoint.value,sizeof prefix.bytes) && prefix.guard==0xa7);
 CHECK(__os_raw(SYSCALL_GETPEERNAME,udp,(long)&native_endpoint.value,sizeof native_endpoint.value,0,0,0)==-ENOTCONN);
 CHECK(!memcmp(&unchanged_endpoint,&native_endpoint.value,sizeof unchanged_endpoint) && native_endpoint.guard==0xabcd);
 CHECK(recv(udp,message,sizeof message,0)==-1 && errno==EAGAIN);
 CHECK(recv(udp,message,(size_t)-1,0)==-1 && errno==EINVAL);
 CHECK(send(udp,message,1,MSG_PEEK)==-1 && errno==EOPNOTSUPP);
 struct sockaddr_in remote={.sin_family=AF_INET,.sin_port=htons(MUSL_TEST_UDP_PORT)};
 unsigned char host[4]={10,0,2,2};
 memcpy(&remote.sin_addr,host,4);
 struct iovec outgoing[2]={{"data",4},{"gram",4}};
 struct msghdr send_header={.msg_name=&remote,.msg_namelen=sizeof remote,
     .msg_iov=outgoing,.msg_iovlen=2};
 CHECK(sendmsg(udp,&send_header,MSG_NOSIGNAL)==8);
 CHECK(fcntl(udp,F_SETFL,0)==0);
 length=sizeof remote;
 struct iovec incoming[2]={{message,1},{message+1,2}};
 struct msghdr recv_header={.msg_name=&remote,.msg_namelen=sizeof remote,
     .msg_iov=incoming,.msg_iovlen=2};
 CHECK(recvmsg(udp,&recv_header,MSG_TRUNC)==8 && !memcmp(message,"dat",3));
 CHECK((recv_header.msg_flags&MSG_TRUNC) && recv_header.msg_namelen==sizeof remote &&
     ntohs(remote.sin_port)==MUSL_TEST_UDP_PORT);
 CHECK(close(udp)==0);
 int tcp=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
 CHECK(tcp>=0);
 remote.sin_port=htons(MUSL_TEST_TCP_PORT);
 CHECK(connect(tcp,(struct sockaddr *)&remote,sizeof remote)==0);
 length=sizeof remote;
 CHECK(getpeername(tcp,(struct sockaddr *)&remote,&length)==0 && ntohs(remote.sin_port)==MUSL_TEST_TCP_PORT);
 CHECK(__os_raw(SYSCALL_GETPEERNAME,tcp,(long)&native_endpoint.value,sizeof native_endpoint.value,0,0,0)==sizeof native_endpoint.value);
 CHECK(native_endpoint.value.port==ntohs(remote.sin_port) &&
     !memcmp(native_endpoint.value.address,&remote.sin_addr,4) && native_endpoint.guard==0xabcd);
 CHECK(__os_raw(SYSCALL_GETSOCKNAME,tcp,1,sizeof native_endpoint.value,0,0,0)==-EFAULT);
 CHECK(send(tcp,"hello",5,0)==5);
 memset(message,0,sizeof message);
 int total=0;
 while(total<5) { int got=recv(tcp,message+total,5-total,0); CHECK(got>0); total+=got; }
 CHECK(!strcmp(message,"world") && close(tcp)==0);
 CHECK(socket(AF_INET6,SOCK_STREAM,0)==-1 && errno==EAFNOSUPPORT);
 return 0;
}
static volatile sig_atomic_t sleep_interrupted;
static void handle_sleep_signal(int signo)
{
 if(signo==SIGUSR1) sleep_interrupted=1;
}
static int nanosleep_check(void)
{
 struct timespec zero={0}, before, after;
 struct { struct timespec value; uint64_t guard; } clock_value={.guard=0xfedcba9876543210ULL};
 CHECK(clock_gettime(CLOCK_MONOTONIC,&clock_value.value)==0);
 CHECK(clock_value.value.tv_sec>=0 && clock_value.value.tv_nsec>=0 &&
     clock_value.value.tv_nsec<1000000000 && clock_value.guard==0xfedcba9876543210ULL);
 CHECK(__os_raw(SYSCALL_CLOCK_GETTIME,1,1,0,0,0,0)==-EFAULT);
 CHECK(clock_gettime((clockid_t)-1,&clock_value.value)==-1 && errno==EINVAL);
 CHECK(clock_value.guard==0xfedcba9876543210ULL);
 struct { struct timespec value; uint64_t guard; } remaining={.guard=0x12345678abcdef00ULL};
 CHECK(nanosleep(&zero,NULL)==0);
 struct timespec invalid={-1,0};
 CHECK(nanosleep(&invalid,NULL)==-1 && errno==EINVAL);
 invalid=(struct timespec){0,1000000000};
 CHECK(nanosleep(&invalid,NULL)==-1 && errno==EINVAL);
 invalid=(struct timespec){INT64_MAX,0};
 CHECK(nanosleep(&invalid,NULL)==-1 && errno==EINVAL);
 CHECK(__os_raw(SYSCALL_NANOSLEEP,1,0,0,0,0,0)==-EFAULT);
 CHECK(__os_raw(SYSCALL_NANOSLEEP,(long)&zero,1,0,0,0,0)==-EFAULT);
 struct timespec short_wait={0,20000000};
 CHECK(clock_gettime(CLOCK_MONOTONIC,&before)==0 && nanosleep(&short_wait,NULL)==0);
 CHECK(clock_gettime(CLOCK_MONOTONIC,&after)==0);
 CHECK((after.tv_sec-before.tv_sec)*1000000000LL+after.tv_nsec-before.tv_nsec>=20000000);
 struct sigaction action={.sa_handler=handle_sleep_signal}, previous;
 sigemptyset(&action.sa_mask);
 CHECK(sigaction(SIGUSR1,&action,&previous)==0);
 pid_t child=fork(); CHECK(child>=0);
 if(!child) {
  struct timespec delay={0,50000000};
  if(nanosleep(&delay,NULL)<0 || kill(getppid(),SIGUSR1)<0) _exit(91);
  _exit(0);
 }
 struct timespec request={2,0};
 CHECK(nanosleep(&request,&remaining.value)==-1 && errno==EINTR && sleep_interrupted);
 CHECK(remaining.value.tv_sec>=0 && remaining.value.tv_sec<=2 &&
     remaining.value.tv_nsec>=0 && remaining.value.tv_nsec<1000000000);
 CHECK(remaining.value.tv_sec || remaining.value.tv_nsec);
 CHECK(remaining.guard==0x12345678abcdef00ULL);
 CHECK(sigaction(SIGUSR1,&previous,NULL)==0);
 int status;
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
 return 0;
}
static int procfs_directory_check(void)
{
 const char *names[]={"self","uptime","meminfo","cpuinfo","modules","pci","usb",
     "disks","cmdline","netpci","dapi","audiopci","hdacodecs","hdatest"};
 DIR *directory=opendir("/proc"); CHECK(directory);
 uint64_t inodes[sizeof names/sizeof names[0]];
 for(size_t i=0;i<sizeof names/sizeof names[0];i++) {
  struct dirent *entry=readdir(directory);
  CHECK(entry && !strcmp(entry->d_name,names[i]) && entry->d_ino);
  CHECK(entry->d_type==(i==0 ? DT_DIR : DT_REG));
  inodes[i]=entry->d_ino;
  for(size_t j=0;j<i;j++) CHECK(inodes[j]!=inodes[i]);
 }
 errno=0;
 CHECK(!readdir(directory) && !errno && closedir(directory)==0);
 struct stat st;
 CHECK(stat("/proc/netif",&st)==-1 && errno==ENOENT);
 CHECK(stat("/proc/dapi",&st)==0 && st.st_ino==inodes[10]);
 return 0;
}
static int sysctl_check(void)
{
 struct os_process_info processes[OS_PROCESS_INFO_MAX];
 size_t process_bytes=0;
 CHECK(sysctlbyname("kern.proc.all",NULL,&process_bytes,NULL,0)==0);
 CHECK(process_bytes>0 && process_bytes<=sizeof processes && process_bytes%sizeof processes[0]==0);
 process_bytes=sizeof processes;
 CHECK(sysctlbyname("kern.proc.all",processes,&process_bytes,NULL,0)==0);
 long process_count=(long)(process_bytes/sizeof processes[0]);
 CHECK(process_count>0 && process_count<=OS_PROCESS_INFO_MAX);
 int found_self=0;
 for(long i=0;i<process_count;i++) {
  CHECK(memchr(processes[i].name,0,sizeof processes[i].name)!=NULL);
  CHECK(processes[i].state<=OS_PROCESS_ZOMBIE);
  if(processes[i].pid==(uint64_t)getpid()) {
   found_self=1;
   CHECK(processes[i].uid==geteuid() && processes[i].parent_pid==(uint64_t)getppid());
  }
 }
 CHECK(found_self);
 pid_t snapshot_child=fork(); CHECK(snapshot_child>=0);
 if(!snapshot_child) _exit(0);
 int process_mib[]={CTL_KERN,KERN_PROC_ALL};
 struct { unsigned char bytes[sizeof(struct os_process_info)+1]; unsigned char guard; } short_snapshot;
 memset(&short_snapshot,0xa5,sizeof short_snapshot);
 process_bytes=sizeof short_snapshot.bytes;
 CHECK(sysctl(process_mib,2,short_snapshot.bytes,&process_bytes,NULL,0)==-1 && errno==ENOMEM);
 CHECK(process_bytes==sizeof short_snapshot.bytes && short_snapshot.guard==0xa5);
 CHECK(waitpid(snapshot_child,NULL,0)==snapshot_child);
 process_bytes=sizeof processes;
 CHECK(sysctl(process_mib,2,(void *)1,&process_bytes,NULL,0)==-1 && errno==EFAULT);
 CHECK(process_bytes==sizeof processes);
 process_bytes=0;
 CHECK(sysctl(process_mib,2,processes,&process_bytes,NULL,0)==-1 && errno==ENOMEM && process_bytes==0);
 process_bytes=SIZE_MAX;
 CHECK(sysctl(process_mib,2,processes,&process_bytes,NULL,0)==0 && process_bytes<=sizeof processes);
 CHECK(sysctl(process_mib,2,NULL,&process_bytes,"x",1)==-1 && errno==EPERM);
 struct utsname identity;
 CHECK(uname(&identity)==0);
 CHECK(!strcmp(identity.sysname,"OpenSillicon") && !strcmp(identity.machine,"amd64"));
 CHECK(!strcmp(identity.nodename,"opensillicon") && !strcmp(identity.release,"development"));
 CHECK(strstr(identity.version,identity.sysname)!=NULL);
 CHECK(uname(NULL)==-1 && errno==EFAULT);
 static const struct { const char *name; int mib[2]; int kind; } nodes[]={
#define CHECK_NODE(symbol,group,value,text,kind) {text,{group,symbol},kind},
  OS_SYSCTL_NODES(CHECK_NODE)
#undef CHECK_NODE
 };
 for(size_t i=0;i<sizeof nodes/sizeof nodes[0];i++) {
  size_t bytes=0;
  int query=sysctl(nodes[i].mib,2,NULL,&bytes,NULL,0);
  if(nodes[i].mib[0]==CTL_NET && query<0 && errno==EAGAIN) continue;
  CHECK(query==0 && bytes>0);
  if(nodes[i].kind==OS_SYSCTL_INT32) CHECK(bytes==sizeof(int32_t));
  if(nodes[i].kind==OS_SYSCTL_UINT64) CHECK(bytes==sizeof(uint64_t));
  if(nodes[i].kind==OS_SYSCTL_STRING) {
   char text[128]; size_t capacity=sizeof text;
   CHECK(sysctlbyname(nodes[i].name,text,&capacity,NULL,0)==0 && capacity==bytes && text[bytes-1]==0);
  }
 }
 size_t length=0;
 CHECK(sysctlbyname("kern.ostype",NULL,&length,NULL,0)==0 && length==sizeof("OpenSillicon"));
 struct { char text[32]; uint64_t guard; } output={.guard=0xabcdef1234567890ULL};
 memset(output.text,0xa5,sizeof output.text);
 length=4;
 CHECK(sysctlbyname("kern.ostype",output.text,&length,NULL,0)==-1 && errno==ENOMEM);
 CHECK(length==4 && !memcmp(output.text,"Open",4) && (unsigned char)output.text[4]==0xa5);
 CHECK(output.guard==0xabcdef1234567890ULL);
 length=sizeof output.text;
 CHECK(sysctlbyname("kern.ostype",output.text,&length,NULL,0)==0 && !strcmp(output.text,"OpenSillicon"));
 CHECK(length==sizeof("OpenSillicon"));
 int mib[2]={777,888}; size_t count=1;
 CHECK(sysctlnametomib("hw.pagesize",mib,&count)==-1 && errno==ENOMEM);
 CHECK(count==2 && mib[0]==777 && mib[1]==888);
 CHECK(sysctlnametomib("hw.pagesize",mib,&count)==0 && mib[0]==CTL_HW && mib[1]==HW_PAGESIZE);
 struct { int value; unsigned guard; } page={0,0xaabbccdd};
 length=sizeof page.value;
 CHECK(sysctl(mib,2,&page.value,&length,NULL,0)==0 && page.value==4096 && length==sizeof page.value);
 CHECK(page.guard==0xaabbccdd);
 CHECK(sysctl(mib,2,NULL,NULL,NULL,0)==0);
 CHECK(sysctl(mib,1,NULL,NULL,NULL,0)==-1 && errno==EINVAL);
 CHECK(sysctl(mib,3,NULL,NULL,NULL,0)==-1 && errno==EINVAL);
 CHECK(sysctl(mib,2,NULL,NULL,&page.value,sizeof page.value)==-1 && errno==EPERM);
 CHECK(sysctl(mib,2,NULL,NULL,NULL,1)==-1 && errno==EINVAL);
 CHECK(sysctlbyname("hw.missing",NULL,&length,NULL,0)==-1 && errno==ENOENT);
 CHECK(sysctlbyname(NULL,NULL,&length,NULL,0)==-1 && errno==EFAULT);
 int max_files; length=sizeof max_files;
 CHECK(sysctlbyname("kern.maxfilesperproc",&max_files,&length,NULL,0)==0 && max_files>3);
 struct rlimit limit;
 CHECK(getrlimit(RLIMIT_NOFILE,&limit)==0 && limit.rlim_cur==(rlim_t)max_files && limit.rlim_max==limit.rlim_cur);
 CHECK(sysconf(_SC_OPEN_MAX)==max_files && getdtablesize()==max_files);
 CHECK(getrlimit(RLIMIT_NOFILE,NULL)==-1 && errno==EFAULT);
 CHECK(getrlimit(RLIMIT_DATA,&limit)==-1 && errno==ENOTSUP);
 int cpus=0; length=sizeof cpus;
 CHECK(sysctlbyname("hw.ncpu",&cpus,&length,NULL,0)==0 && cpus>=1);
 uint64_t memory=0,free_pages=0; length=sizeof memory;
 CHECK(sysctlbyname("hw.physmem",&memory,&length,NULL,0)==0 && memory>0 && memory%4096==0);
 length=sizeof free_pages;
 CHECK(sysctlbyname("vm.freepages",&free_pages,&length,NULL,0)==0 && free_pages<=memory/4096);
 length=0;
 CHECK(sysctl(mib,2,output.text,&length,NULL,0)==-1 && errno==ENOMEM && length==0);
 length=SIZE_MAX;
 CHECK(sysctl(mib,2,&page.value,&length,NULL,0)==0 && length==sizeof page.value);
 CHECK(__os_raw(SYSCALL_SYSCTL,1,2,0,0,0,0)==-EFAULT);
 CHECK(__os_raw(SYSCALL_SYSCTL,(long)mib,2,0,1,0,0)==-EFAULT);
 length=sizeof page.value;
 CHECK(__os_raw(SYSCALL_SYSCTL,(long)mib,2,1,(long)&length,0,0)==-EFAULT);
 CHECK(length==sizeof page.value && page.guard==0xaabbccdd);
 return 0;
}
int main(int argc, char **argv)
{
 if(argc==3 && !strcmp(argv[1],"--shm-exec-check")) {
  const void *old_mapping=(void *)(uintptr_t)strtoull(argv[2],NULL,10);
  return write(1,old_mapping,1)==-1 && errno==EFAULT ? 30 : 123;
 }
 if(argc>=2 && !strcmp(argv[1],"--exec-limits-check")) {
  size_t arg_bytes=0, env_bytes=0, env_count=0;
  for(int i=0;i<argc;i++) arg_bytes+=strlen(argv[i])+1;
  while(environ[env_count]) env_bytes+=strlen(environ[env_count++])+1;
  return argc==OS_EXEC_ARG_MAX && arg_bytes==OS_EXEC_ARG_BYTES_MAX &&
      env_count==OS_EXEC_ENV_MAX && env_bytes==OS_EXEC_ENV_BYTES_MAX &&
      constructed==42 && tls==123 ? 28 : 125;
 }
 if(argc==2 && !strcmp(argv[1],"--shell-tty-check")) {
  struct termios attributes;
  if(tcgetattr(0,&attributes) || !(attributes.c_lflag&ICANON) ||
      !(attributes.c_lflag&ECHO) || attributes.c_cc[VERASE]!=8 ||
      attributes.c_oflag!=(OPOST|ONLCR)) return 123;
  puts("SHELL_TTY_OK");
  return 0;
 }
 if(argc==2 && !strcmp(argv[1],"--shell-path-check")) {
  const char *env=getenv("MUSL_SHELL_CHECK"), *term=getenv("TERM");
  if(!env || strcmp(env,"present") || !term || strcmp(term,"xterm")) return 122;
  puts("SHELL_PATH_OK");
  return 0;
 }
 if(argc==3 && !strcmp(argv[1],"--cloexec-check")) {
  return fcntl(atoi(argv[2]),F_GETFD)==-1 && errno==EBADF ? 29 : 104;
 }
 if(argc==2 && !strcmp(argv[1],"--exec-check")) {
  const char *env=getenv("MUSL_EXEC_CHECK");
  return env && !strcmp(env,"present") && tls==123 && constructed==42 ? 27 : 83;
 }
 if(argc<0 || (argc && !argv[0])) return 11;
 if(tls!=123) return 12;
 if(constructed!=42) return 13;
 tls=456;
 char *p=malloc(1024*1024);
 if(!p) return 2;
 memset(p,0x5a,1024*1024);
 p=realloc(p,2*1024*1024);
 if(!p || p[999999]!=0x5a) return 3;
 free(p);
 errno=0;
 if(close(-1)!=-1 || errno!=EBADF || tls!=456) return 4;
 errno=0;
 if(syscall(SYS_ptrace,0,0,0)!=-1 || errno!=ENOSYS) return 6;
 CHECK(__os_raw(SYSCALL_GET_ABI_VERSION,0,0,0,0,0,0)==SYSCALL_ABI_VERSION);
 const long retired[]={4,5,6,7,8,9,10,14,17,23,33,34,41,43,44,46,53,58,63,81,82,84,88,89,91,92,93,94,101,109};
 for(size_t i=0;i<sizeof retired/sizeof retired[0];i++)
  CHECK(__os_raw(retired[i],0,0,0,0,0,0)==-78);
 CHECK(__os_raw(SYSCALL_EXECVE,(long)"/sbin/init",1,0,0,0,0)==-EFAULT);
 struct timespec t;
 if(clock_gettime(CLOCK_MONOTONIC,&t)) return 5;
 int sleep_result=nanosleep_check();
 if(sleep_result) return sleep_result;
 int procfs_result=procfs_directory_check();
 if(procfs_result) return procfs_result;
 int sysctl_result=sysctl_check();
 if(sysctl_result) return sysctl_result;
 int large_io_result=large_iovec_check();
 if(large_io_result) return large_io_result;
 int file_result=files_check();
 if(file_result) return file_result;
 int at_result=openat_check();
 if(at_result) return at_result;
 int capability_result=directory_capability_check();
 if(capability_result) return capability_result;
 int append_result=concurrent_append_check();
 if(append_result) return append_result;
 int duplication_result=duplication_check();
 if(duplication_result) return duplication_result;
 int signal_result=signals_check();
 if(signal_result) return signal_result;
 int action_result=signal_action_check();
 if(action_result) return action_result;
 int nested_result=nested_signal_check();
 if(nested_result) return nested_result;
 int power_result=power_permissions_check();
 if(power_result) return power_result;
 int module_result=module_file_check();
 if(module_result) return module_result;
 int shared_result=shared_mapping_check();
 if(shared_result) return shared_result;
 int exec_result=exec_limits_check();
 if(exec_result) return exec_result;
 int process_result=process_check();
 if(process_result) return process_result;
 int shell_result=shell_path_check();
 if(shell_result) return shell_result;
 int shell_pty_result=shell_pty_check();
 if(shell_pty_result) return shell_pty_result;
 int terminal_result=terminal_check();
 if(terminal_result) return terminal_result;
 int curses_result=curses_check();
 if(curses_result) return curses_result;
 int poll_result=poll_check();
 if(poll_result) return poll_result;
 int flock_result=flock_check();
 if(flock_result) return flock_result;
 int network_result=network_check();
 if(network_result) return network_result;
 int dns_result=dns_check();
 if(dns_result) return dns_result;
 printf("MUSL_PASS pid=%ld tls=%d ctor=%d malloc=ok errno=ok files=ok processes=ok signals=ok sockets=ok poll=ok dns=ok terminal=ok ncurses=ok openat=ok append=ok duplication=ok sigaction=ok nested-signals=ok sysctl=ok nanosleep=ok\n",(long)getpid(),tls,constructed);
 return 0;
}
