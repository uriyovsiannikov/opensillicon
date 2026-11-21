#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
extern const unsigned char java_start[],java_end[],rt_start[],rt_end[],probe_start[],probe_end[],client_start[],client_end[];
static void put(const char *name,const unsigned char *start,const unsigned char *end){FILE*f=fopen(name,"wb");if(!f||fwrite(start,1,end-start,f)!=(size_t)(end-start)||fclose(f))exit(30);}
int main(void){
 void *bad=mmap(0,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(bad!=MAP_FAILED)return 10;
 unsigned char *p=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(p==MAP_FAILED)return 11;
 unsigned char code[]={0xb8,42,0,0,0,0xc3};memcpy(p,code,sizeof code);
 if(mprotect(p,4096,PROT_READ|PROT_WRITE|PROT_EXEC)==0)return 12;
 if(mprotect(p,4096,PROT_READ|PROT_EXEC))return 13;
 if(((int(*)(void))p)()!=42)return 14;
 pid_t child=fork();int status;
 if(child==0)_exit(((int(*)(void))p)()==42?0:1);
 if(child<0||waitpid(child,&status,0)<0||status)return 15;
 if(mprotect(p,4096,PROT_READ|PROT_WRITE))return 16;
 p[1]=43;if(mprotect(p,4096,PROT_READ|PROT_EXEC)||((int(*)(void))p)()!=43)return 17;
 if(munmap(p,4096))return 18;
 puts("JIT_MEMORY_PASS RW/RX, RWX rejection, fork, repatch, unmap");fflush(stdout);
 put("/tmp/java",java_start,java_end);chmod("/tmp/java",0755);
 put("/tmp/rt.jar",rt_start,rt_end);put("/tmp/probe.jar",probe_start,probe_end);
 put("/tmp/client.jar",client_start,client_end);
 for(int mode=0;mode<2;mode++){
  puts(mode ? "CHECK_INTERPRETER" : "CHECK_JIT"); fflush(stdout);
  pid_t child=fork();
  if(child==0){
   char *jit[]={"java",
#ifdef JIT_PROFILE_CHECK
   "-Dopensillicon.profile=1",
#ifdef JIT_METHOD_PROFILE_CHECK
   "-Dopensillicon.profile.methods=1",
#endif
#endif
   "-bootclasspath","/tmp/rt.jar","-cp","/tmp/probe.jar:/tmp/client.jar",
#ifdef JIT_PROFILE_CHECK
   "JitProbe",
#else
   "JitTerrainProbe",
#endif
   0};
   char *interpreted[]={"java","-Xint",
#ifdef JIT_PROFILE_CHECK
   "-Dopensillicon.profile=1",
#ifdef JIT_METHOD_PROFILE_CHECK
   "-Dopensillicon.profile.methods=1",
#endif
   "-Dopensillicon.profile.log=stderr",
#endif
   "-bootclasspath","/tmp/rt.jar","-cp","/tmp/probe.jar:/tmp/client.jar",
#ifdef JIT_PROFILE_CHECK
   "JitProbe",
#else
   "JitTerrainProbe",
#endif
   0};
   execv("/tmp/java",mode?interpreted:jit); _exit(31);
  }
  if(child<0 || waitpid(child,&status,0)<0 || status) return 32+mode;
#ifdef JIT_PROFILE_CHECK
  if(mode==0){
   FILE *log=fopen("/tmp/minijvm-profile.log","rb");if(!log)return 40;
   char buffer[1024];size_t n,total=0;
   while((n=fread(buffer,1,sizeof buffer,log))!=0){fwrite(buffer,1,n,stdout);total+=n;}
   fclose(log);if(!total)return 41;
   puts("PROFILE_LOG_FILE_PASS");fflush(stdout);
  }
#endif
 }
 puts("JIT_CHECK_ALL_PASS");fflush(stdout);return 0;
}
