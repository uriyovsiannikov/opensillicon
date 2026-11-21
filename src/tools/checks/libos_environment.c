#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdint.h>
#include "abi/syscall.h"
extern long __libos_call(long,long,long,long,long,long,long);
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <pty.h>
#include <poll.h>
#include <signal.h>
#include <curses.h>
#include <term.h>
static void require(int ok,const char *message){if(!ok){fprintf(stderr,"ENV_FAIL %s errno=%d\n",message,errno);exit(1);}}
static void launch(int mode,char *const env[]){
 int p=fork(),status=0;require(p>=0,"fork");
 if(!p){char *args[]={"probe",mode==1?"inherit":(mode==2||mode==4)?"empty":mode==3?"max":"custom",NULL};
  if(mode==4)__libos_call(SYSCALL_EXEC_PATH_ARGV,(long)"/sbin/init",2,(long)args,0,0,0);
  else if(mode==1)execl("/sbin/init","probe","inherit",(char*)NULL);else execve("/sbin/init",args,env);
  perror("exec");_exit(2);}
 require(waitpid(p,&status,0)==p&&!status,"child");
}
extern const unsigned char app0_start[],app0_end[],app1_start[],app1_end[],app2_start[],app2_end[],app3_start[],app3_end[],app4_start[],app4_end[],app5_start[],app5_end[];
static void put_app(const char *path,const unsigned char *start,const unsigned char *end){FILE *f=fopen(path,"wb");require(f&&fwrite(start,1,end-start,f)==(size_t)(end-start)&&!fclose(f)&&!chmod(path,0755),"install app");}
static void pty_check(const char *path,int shell_mode,int explicit_term) {
 int master,slave,status=0,ended=0;struct winsize size={25,80,0,0};
 require(!openpty(&master,&slave,NULL,NULL,&size),"openpty");
 int p=fork();require(p>=0,"pty fork");
 if(!p){dup2(slave,0);dup2(slave,1);dup2(slave,2);close(master);close(slave);
  char *args[]={(char *)path,NULL};char *env[]={"TERM=screen-256color","DISPLAY=:9","CHECK_VALUE=space value","EMPTY=","EXTRA=inherited",NULL};
  execve(path,args,explicit_term?env:NULL);_exit(2);}
 close(slave);
 if(shell_mode){const char *line="/sbin/init inherit\nexit\n";require(write(master,line,strlen(line))==(ssize_t)strlen(line),"shell input");}
 char output[16384];size_t used=0;int escape=0;
 for(int i=0;i<20;i++) {
  struct pollfd fd={master,POLLIN,0};
  if(poll(&fd,1,100)>0 && (fd.revents&POLLIN)) {char buffer[1024];ssize_t n=read(master,buffer,sizeof buffer);if(n>0){for(ssize_t j=0;j<n;j++)if(buffer[j]==27)escape=1;if((size_t)n>sizeof(output)-1-used)n=sizeof(output)-1-used;memcpy(output+used,buffer,n);used+=n;}}
  if(waitpid(p,&status,WNOHANG)==p){ended=1;break;}
 }
 output[used]=0;
 if(shell_mode)require(strstr(output,"ENV_CHILD_PASS")!=NULL,"shell preserved environment and auxv");
 else {if(ended||!escape)fprintf(stderr,"APP_FAIL %s ended=%d escape=%d status=%d: %s\n",path,ended,escape,status,output);require(!ended&&escape,"ncurses application startup");}
 require(!strstr(output,"unknown terminal")&&!strstr(output,"Error opening terminal"),"terminal recognized");
 if(!ended){kill(p,SIGTERM);require(waitpid(p,&status,0)==p,"reap app");}
 close(master);printf("PTY_APP_PASS %s TERM=%s\n",path,explicit_term?"screen-256color":"default");fflush(stdout);
}
int main(int argc,char **argv,char **envp){
 require(envp==environ,"crt environ pointer");
 if(argc>1){
  size_t n=0;while(envp[n])n++;
  uintptr_t *aux=(uintptr_t *)(envp+n+1);require(aux[0]==3&&aux[2]==4&&aux[3]==56,"auxv after env");
  if(!strcmp(argv[1],"empty")){require(n==0&&!strcmp(getenv("TERM"),"xterm"),"empty environment fallback");return 0;}
  if(!strcmp(argv[1],"max")){require(n==64,"maximum environment");return 0;}
  require(!strcmp(getenv("TERM"),"screen-256color")&&!strcmp(getenv("DISPLAY"),":9"),"TERM/DISPLAY inherited");
  require(!strcmp(getenv("CHECK_VALUE"),"space value")&&!strcmp(getenv("EMPTY"),""),"values preserved");
  if(!strcmp(argv[1],"custom")){require(setenv("EXTRA","inherited",1)==0,"setenv");launch(1,NULL);}else require(!strcmp(getenv("EXTRA"),"inherited"),"execl inherits changes");
  puts("ENV_CHILD_PASS");fflush(stdout);return 0;
 }
 require(!strcmp(getenv("TERM"),"xterm"),"legacy launch default TERM");
 launch(4,NULL);
#ifndef LEGACY_KERNEL_CHECK
 char *custom[]={"TERM=screen-256color","DISPLAY=:9","CHECK_VALUE=space value","EMPTY=",NULL};
 launch(0,custom);launch(2,NULL);
 char *args[]={"probe","max",NULL};char *large[66];for(int i=0;i<65;i++)large[i]="X=y";large[64]=NULL;launch(3,large);large[64]="X=z";large[65]=NULL;
 require(execve("/sbin/init",args,large)<0&&errno==E2BIG,"oversized vector rejected");
 require(execve("/sbin/init",args,(char *const *)1)<0&&errno==EFAULT,"bad vector rejected");
 char *big=malloc(8193);require(big!=NULL,"allocate");memset(big,'X',8192);big[8192]=0;char *oversized[]={big,NULL};
 require(execve("/sbin/init",args,oversized)<0&&errno==E2BIG,"oversized strings rejected");free(big);
 fd_set descriptors;struct timeval timeout={0,0};FD_ZERO(&descriptors);FD_SET(0,&descriptors);
 require(select(FD_SETSIZE,&descriptors,NULL,NULL,&timeout)>=0,"select with unused high descriptors");
 FD_ZERO(&descriptors);FD_SET(40,&descriptors);
 require(select(FD_SETSIZE,&descriptors,NULL,NULL,&timeout)<0&&errno==EBADF,"select invalid requested descriptor");
 #endif
 const char *types[]={"xterm","xterm-256color","screen","screen-256color","vt100","vt220","linux","dumb"};
 for(unsigned i=0;i<sizeof types/sizeof *types;i++){int error=0;require(setupterm(types[i],1,&error)==OK&&error==1,types[i]);if(strstr(types[i],"256"))require(tigetnum("colors")==256,"256 colors");del_curterm(cur_term);}
 put_app("/tmp/cmatrix",app0_start,app0_end);put_app("/tmp/nsnake",app1_start,app1_end);put_app("/tmp/moon-buggy",app2_start,app2_end);put_app("/tmp/pipes.sh",app3_start,app3_end);put_app("/tmp/nano",app4_start,app4_end);put_app("/tmp/htop",app5_start,app5_end);
 #ifndef LEGACY_KERNEL_CHECK
 pty_check("/bin/sh",1,1);
 #endif
 const char *apps[]={"/tmp/cmatrix","/tmp/nsnake","/tmp/moon-buggy","/tmp/pipes.sh","/tmp/nano","/tmp/htop"};
 for(unsigned i=0;i<6;i++){pty_check(apps[i],0,0);pty_check(apps[i],0,1);}
#ifdef LEGACY_KERNEL_CHECK
 puts("ENVIRONMENT_PASS legacy-kernel launch compatibility; NCURSES_TERMINALS_PASS");
#else
 puts("ENVIRONMENT_PASS execve/execl, CRT, auxv, empty/max/bad vectors; NCURSES_TERMINALS_PASS");
#endif
 fflush(stdout);return 0;
}
