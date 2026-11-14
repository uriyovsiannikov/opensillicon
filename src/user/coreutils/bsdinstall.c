#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common/uuid.h"
#include "../storage/storage.h"
#include "../storage/install_copy.h"
#define TARGET "/mnt"
static FILE *journal;
static const char *stage="preflight";
static void note(const char *format,...)
{
 va_list args;va_start(args,format);vprintf(format,args);va_end(args);fflush(stdout);
 if(journal){va_start(args,format);vfprintf(journal,format,args);va_end(args);fflush(journal);}
}
static int line(char *out,size_t size)
{
 if(!fgets(out,(int)size,stdin))return -1;
 size_t n=strlen(out);
 if(!n || out[n-1]!='\n') {int c;while((c=fgetc(stdin))!='\n' && c!=EOF){}return -1;}
 while(n && (out[n-1]=='\n'||out[n-1]=='\r'))out[--n]=0;
 return 0;
}
static int disk_path(const char *text,char out[32])
{
 const char *p=text;
 if(!strncmp(p,"/dev/",5))p+=5;
 if(strncmp(p,"ada",3))return -EINVAL;
 const char *number=p+3;if(!*number)return -EINVAL;
 for(const char *q=number;*q;q++)if(*q<'0'||*q>'9')return -EINVAL;
 if(strlen(p)+5>=32)return -ENAMETOOLONG;
 strcpy(out,"/dev/");strcat(out,p);return 0;
}
static int live_root(void)
{
 int fd=open("/dev/ram0",O_RDONLY);if(fd<0)return 0;
 struct os_disk_partinfo info;
 int live=ioctl(fd,DIOCGPARTINFO,&info)==0 && (info.flags&OS_DISK_MOUNTED);
 close(fd);return live;
}
static int directory(const char *path,unsigned mode)
{
 if(mkdir(path,mode)==0)return 0;
 if(errno!=EEXIST)return -errno;
 struct stat st;if(stat(path,&st)!=0)return -errno;
 return S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
}
static void list_disks(void)
{
 puts("Available SATA disks:");
 for(unsigned i=0;i<7;i++) {
  char path[32],model[41]={0},ident[256]={0};snprintf(path,sizeof(path),"/dev/ada%u",i);
  struct storage_device d;if(storage_open(&d,path,0))continue;
  struct os_disk_partinfo info;
  if(ioctl(d.fd,DIOCGPARTINFO,&info)==0) {
   (void)ioctl(d.fd,DIOCGMODEL,model);(void)ioctl(d.fd,DIOCGIDENT,ident);
   printf("  %s  %llu MiB  %s  serial=%s%s\n",path,(unsigned long long)(d.io.bytes/(1024*1024)),
    model,ident,(info.flags&OS_DISK_RAW_WRITE_ALLOWED) ? "" : "  [IN USE]");
  }
  storage_close(&d);
 }
}
static int boot_sources(void)
{
 static const char *paths[]={"/boot/BOOTX64.EFI","/boot/kernel.elf","/boot/init.cpi"};
 for(unsigned i=0;i<3;i++) {
  int fd=open(paths[i],O_RDONLY);if(fd<0)return -errno;
  unsigned char bytes[8];ssize_t n=read(fd,bytes,sizeof(bytes));close(fd);
  if(n!=sizeof(bytes) || (i==0 && memcmp(bytes,"MZ",2)) ||
    (i==1 && memcmp(bytes,"\x7f" "ELF",4)) ||
    (i==2 && memcmp(bytes,"070701",6) && memcmp(bytes,"070702",6)))return -EINVAL;
 }
 int fd=open("/dev/urandom",O_RDONLY);if(fd<0)return -errno;
 unsigned char random[48];ssize_t n=read(fd,random,sizeof(random));close(fd);
 return n==sizeof(random) ? 0 : -EAGAIN;
}
static int run(char **argv)
{
 if(journal)fflush(journal);
 pid_t pid=fork();if(pid<0)return -errno;
 if(pid==0) {
  if(journal && (dup2(fileno(journal),1)<0 || dup2(fileno(journal),2)<0))_exit(126);
  execv(argv[0],argv);_exit(127);
 }
 int status=0;if(waitpid(pid,&status,0)!=pid)return -errno;
 if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) {
  note("%s failed: child status %d\n",stage,status);return -EIO;
 }
 return 0;
}
static int progress(void *context,const char *path,size_t done,size_t total)
{
 (void)context;
 if(journal && (fprintf(journal,"verified %s\n",path)<0 || fflush(journal)!=0))return -EIO;
 if(done%20==0 || done==total) {printf("Copy verified: %u/%u entries\n",(unsigned)done,(unsigned)total);fflush(stdout);}
 return 0;
}
static int save_journal(void)
{
 int error=directory(TARGET "/var",0755);if(!error)error=directory(TARGET "/var/log",0755);
 if(error)return error;
 if(fflush(journal)!=0)return -errno;
 int source=open("/tmp/bsdinstall.log",O_RDONLY);if(source<0)return -errno;
 int output=open(TARGET "/var/log/install.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
 if(output<0){error=-errno;close(source);return error;}
 unsigned char buffer[4096];
 for(;;) {
  ssize_t n=read(source,buffer,sizeof(buffer));if(n<0){error=-errno;break;}if(!n)break;
  for(ssize_t done=0;done<n;){ssize_t w=write(output,buffer+done,(size_t)(n-done));if(w<=0){error=w<0 ? -errno : -EIO;break;}done+=w;}
  if(error)break;
 }
 if(!error && fsync(output)!=0)error=-errno;
 if(close(output)!=0 && !error)error=-errno;
 if(close(source)!=0 && !error)error=-errno;
 return error;
}
int main(int argc,char **argv)
{
 int preview=argc==3 && !strcmp(argv[1],"-N");
 if(argc!=1 && !preview){fputs("usage: bsdinstall [-N /dev/ada0]\n",stderr);return 2;}
 if(geteuid()!=0){fputs("bsdinstall: select OpenSillicon Installer in the Live boot menu (root required).\n",stderr);return 1;}
 if(!live_root()){fputs("bsdinstall: installation is allowed only from the Live RAM root.\n",stderr);return 1;}
 journal=fopen("/tmp/bsdinstall.log","w");if(!journal){perror("bsdinstall log");return 1;}
 note("OpenSillicon Installer - UEFI/GPT, whole SATA disk\n");
 note("All data on the selected disk will be replaced. Log: /tmp/bsdinstall.log\n");
 char answer[128],path[32];
 if(preview){if(disk_path(argv[2],path)){fclose(journal);return 2;}}
 else {
  list_disks();fputs("Target disk (ada0 or /dev/ada0; q to cancel): ",stdout);fflush(stdout);
  if(line(answer,sizeof(answer)) || !strcmp(answer,"q")){note("Installation cancelled; no disk writes.\n");fclose(journal);return 0;}
  if(disk_path(answer,path)){note("Invalid disk name; no disk writes.\n");fclose(journal);return 2;}
 }
 struct storage_device d={.fd=-1};struct os_disk_partinfo original;
 char model[41]={0},ident[256]={0};struct gpt_layout plan;struct sfs_layout fsplan;
 struct install_tree tree={0};int mounted=0,error=storage_open(&d,path,0);
 if(!error && ioctl(d.fd,DIOCGPARTINFO,&original)!=0)error=-errno;
 if(!error && ((original.flags&OS_DISK_PARTITION) || !(original.flags&OS_DISK_RAW_WRITE_ALLOWED)))error=-EBUSY;
 if(!error && (ioctl(d.fd,DIOCGMODEL,model)!=0 || ioctl(d.fd,DIOCGIDENT,ident)!=0))error=-errno;
 if(!error)error=gpt_layout_plan(&d.io,512,&plan);
 unsigned char probe[4096];
 if(!error)error=d.io.read(d.io.context,0,probe,d.io.sector);
 if(!error)error=d.io.read(d.io.context,d.io.bytes-d.io.sector,probe,d.io.sector);
 if(!error && ioctl(d.fd,DIOCGFLUSH)!=0)error=-errno;
 if(!error)error=boot_sources();
 if(!error)error=directory(TARGET,0755);
 if(!error){note("Scanning and verifying Live files...\n");error=install_tree_scan(&tree,"/");}
 if(!error)error=install_tree_target_check(&tree,TARGET);
 uint32_t inodes=4096;
 while(inodes<tree.count+16)inodes*=2;
 if(!error) {
  struct storage_io target=d.io;target.bytes=plan.root_sectors*d.io.sector;
  error=sfs_plan(&target,inodes,&fsplan);
  if(!error && tree.required_blocks>fsplan.blocks-fsplan.data_start)error=-ENOSPC;
 }
 if(error)goto failed;
 note("Target: %s | %s | serial=%s | %llu bytes\n",path,model,ident,(unsigned long long)d.io.bytes);
 note("Plan: GPT; p1 EFI 512 MiB; p2 SilliconFS %llu MiB, %u inodes\n",
  (unsigned long long)(plan.root_sectors*d.io.sector/(1024*1024)),inodes);
 note("Source: %u entries, %llu file bytes; permissions and owners preserved\n",(unsigned)tree.count,(unsigned long long)tree.file_bytes);
 if(preview){note("Preview complete; no partition or filesystem writes.\n");storage_close(&d);install_tree_free(&tree);fclose(journal);return 0;}
 char confirmation[64];snprintf(confirmation,sizeof(confirmation),"ERASE %s",path);
 note("To erase this disk, type: %s\nConfirmation: ",confirmation);
 if(line(answer,sizeof(answer)) || strcmp(answer,confirmation)) {
  note("Installation cancelled; no disk writes.\n");storage_close(&d);install_tree_free(&tree);fclose(journal);return 0;
 }
 error=install_tree_recheck(&tree);if(error)goto failed;
 /* Revalidate identity and in-use state immediately before launching writers. */
 char current_ident[256];struct os_disk_partinfo current;
 if(ioctl(d.fd,DIOCGPARTINFO,&current)!=0 || ioctl(d.fd,DIOCGIDENT,current_ident)!=0)error=-errno;
 if(!error && (memcmp(&current,&original,sizeof(current)) || strcmp(current_ident,ident)))error=-EAGAIN;
 if(error)goto failed;
 storage_close(&d);
 stage="GPT creation";note("[1/6] %s\n",stage);
 char *gpart[]={"/bin/gpart","bootlayout","-F",path,NULL};error=run(gpart);if(error)goto failed;
 char root_path[40],esp_path[40],inode_text[16],uuid[37];
 snprintf(root_path,sizeof(root_path),"%sp2",path);snprintf(esp_path,sizeof(esp_path),"%sp1",path);
 int fd=open(root_path,O_RDONLY);if(fd<0){error=-errno;goto failed;}
 struct os_disk_partinfo rootinfo;
 error=ioctl(fd,DIOCGPARTINFO,&rootinfo)==0 ? 0 : -errno;close(fd);if(error)goto failed;
 uuid_format(rootinfo.uuid,uuid);note("Root PARTUUID=%s\n",uuid);
 snprintf(inode_text,sizeof(inode_text),"%u",inodes);
 stage="root filesystem creation";note("[2/6] %s\n",stage);
 char *newfs[]={"/bin/newfs_silliconfs","-F","-i",inode_text,root_path,NULL};error=run(newfs);if(error)goto failed;
 struct silliconfs_args args={root_path};
 if(mount("silliconfs",TARGET,0,&args)!=0){error=-errno;goto failed;}mounted=1;
 stage="verified system copy";note("[3/6] %s\n",stage);
 error=install_tree_copy(&tree,TARGET,progress,NULL);if(error)goto failed;
 stage="UEFI boot installation";note("[4/6] %s\n",stage);
 char *boot[]={"/bin/installboot","-F","-r",uuid,esp_path,NULL};error=run(boot);if(error)goto failed;
 stage="installation journal and filesystem flush";note("[5/6] %s\n",stage);
 note("All system files and boot payloads verified. Final unmount/flush follows.\n");
 error=save_journal();if(error)goto failed;
 if(unmount(TARGET,0)!=0){error=-errno;goto failed;}mounted=0;
 stage="final disk flush";note("[6/6] %s\n",stage);
 error=storage_open(&d,path,0);if(!error && ioctl(d.fd,DIOCGFLUSH)!=0)error=-errno;
 if(error)goto failed;
 storage_close(&d);install_tree_free(&tree);
 note("INSTALLATION COMPLETE: %s\nRemove the Live USB before booting the installed system.\n",path);
 fputs("Reboot now? [y/N]: ",stdout);fflush(stdout);
 if(!line(answer,sizeof(answer)) && (!strcmp(answer,"y") || !strcmp(answer,"Y"))) {
  stage="reboot";char *reboot[]={"/bin/reboot",NULL};(void)run(reboot);
 }
 fclose(journal);return 0;
failed:
 note("INSTALLATION FAILED at %s (errno %d), path=%s. See /tmp/bsdinstall.log\n",stage,-error,tree.failed_path);
 if(mounted && unmount(TARGET,0)!=0)note("Target remains mounted at %s; close files and retry umount (errno %d).\n",TARGET,errno);
 if(d.fd>=0)storage_close(&d);
 install_tree_free(&tree);fclose(journal);return 1;
}
