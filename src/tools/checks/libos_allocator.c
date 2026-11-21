#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#define CHECK(x) do {if(!(x)){printf("ALLOC_FAIL line=%d %s\n",__LINE__,#x);fflush(stdout);return 1;}}while(0)
static unsigned char *ptrs[3072];
static size_t lengths[3072];
static uint64_t ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
int main(void){
 const size_t sizes[]={0,1,15,16,17,31,32,47,48,63,64,111,112,113,239,240,241,495,496,497,1007,1008,1009,1024,2031,2032,2033,2048,4079,4080,4081,4096,8176,8177,8192,16368,16369,16384,32752,32753,32768,65536};
 for(unsigned i=0;i<3072;i++){
  size_t n=sizes[i%(sizeof sizes/sizeof *sizes)];lengths[i]=n;ptrs[i]=malloc(n);CHECK(ptrs[i]);CHECK(!((uintptr_t)ptrs[i]&15));memset(ptrs[i],(unsigned char)i,n);
 }
 for(unsigned j=0;j<3072;j++){
  unsigned i=(j*257)%3072;size_t n=lengths[i];for(size_t k=0;k<n;k++)CHECK(ptrs[i][k]==(unsigned char)i);
  size_t larger=n+4097;unsigned char *p=realloc(ptrs[i],larger);CHECK(p);for(size_t k=0;k<n;k++)CHECK(p[k]==(unsigned char)i);memset(p,0xa5,larger);
  unsigned char*q=realloc(p,17);CHECK(q);for(unsigned k=0;k<17;k++)CHECK(q[k]==0xa5);free(q);
 }
 volatile size_t huge=SIZE_MAX;errno=0;CHECK(calloc(huge,2)==NULL&&errno==ENOMEM);
 for(unsigned i=0;i<200;i++){size_t n=sizes[i%(sizeof sizes/sizeof *sizes)];unsigned char*p=calloc(n,1);CHECK(p);for(size_t k=0;k<n;k++)CHECK(p[k]==0);free(p);}
 unsigned char *p=malloc(8192);CHECK(p);memset(p,37,8192);int child=fork(),status;CHECK(child>=0);if(!child){for(unsigned i=0;i<8192;i++)if(p[i]!=37)_exit(3);free(p);_exit(0);}CHECK(waitpid(child,&status,0)==child&&status==0);for(unsigned i=0;i<8192;i++)CHECK(p[i]==37);free(p);
 uint64_t start=ms();
 for(unsigned round=0;round<4;round++){
  for(unsigned i=0;i<3072;i++){ptrs[i]=malloc(2048);CHECK(ptrs[i]);ptrs[i][0]=i;ptrs[i][2047]=i>>8;}
  for(unsigned j=0;j<3072;j++){unsigned i=(j*257)%3072;CHECK(ptrs[i][0]==(unsigned char)i&&ptrs[i][2047]==(unsigned char)(i>>8));free(ptrs[i]);}
 }
 printf("ALLOCATOR_PASS elapsed_ms=%llu\n",(unsigned long long)(ms()-start));fflush(stdout);return 0;
}
