#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include "blobs.h"
#define CHECK(x) do{if(!(x)){printf("STARTX_FAIL %d: %s\n",__LINE__,#x);fflush(stdout);return 1;}}while(0)
static int terminal_window(Display *display, Window window, unsigned depth) {
 XClassHint hint; int found=0;
 if(XGetClassHint(display,window,&hint)) {
  found=hint.res_class && !strcmp(hint.res_class,"XTerm");
  if(hint.res_name)XFree(hint.res_name);if(hint.res_class)XFree(hint.res_class);
 }
 if(found || depth==0)return found;
 Window root,parent,*children=NULL;unsigned count=0;
 if(XQueryTree(display,window,&root,&parent,&children,&count)) {
  for(unsigned i=0;i<count && !found;i++)found=terminal_window(display,children[i],depth-1);
  if(children)XFree(children);
 }
 return found;
}
int main(void){
 for(unsigned i=0;i<sizeof(payload)/sizeof(*payload);i++){
  char path[256];strcpy(path,payload[i].name);for(char*p=path+1;*p;p++)if(*p=='/'){*p=0;mkdir(path,0755);*p='/';}
  FILE*f=fopen(path,"wb");CHECK(f);CHECK(fwrite(payload[i].start,1,payload[i].end-payload[i].start,f)==(size_t)(payload[i].end-payload[i].start));CHECK(!fclose(f));CHECK(!chmod(path,0755));
 }
 setenv("HOME","/",1);setenv("DISPLAY",":0",1);setenv("TERM","xterm",1);
 pid_t pid=fork();CHECK(pid>=0);if(!pid){char*args[]={"startx",NULL};execv("/usr/bin/startx",args);_exit(127);}
 int status;for(int i=0;i<60;i++){struct timespec t={0,50000000};nanosleep(&t,NULL);if(waitpid(pid,&status,WNOHANG)==pid){printf("STARTX_EARLY_STATUS=%d\n",status);CHECK(0);}}
 Display*d=XOpenDisplay(":0");CHECK(d);int ready=0;
 for(int i=0;i<100;i++){
  ready=terminal_window(d,DefaultRootWindow(d),4);
  if(ready)break;
  if(waitpid(pid,&status,WNOHANG)==pid){printf("STARTX_EARLY_STATUS=%d\n",status);CHECK(0);}
  struct timespec t={0,100000000};nanosleep(&t,NULL);
 }
 XCloseDisplay(d);CHECK(ready);CHECK(kill(pid,SIGTERM)==0);
 int exited=0;for(int i=0;i<100;i++){if(waitpid(pid,&status,WNOHANG)==pid){exited=1;break;}struct timespec t={0,100000000};nanosleep(&t,NULL);}
 CHECK(exited && WIFEXITED(status) && WEXITSTATUS(status)==0);puts("STARTX_PASS: desktop windows and session signal cleanup");return 0;
}
