#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "abi/audio.h"
static void check(int ok,const char *name){if(!ok){perror(name);exit(40);}}
static int16_t buffer[2048];
static void make_wave(void)
{
    unsigned char h[56]={ 'R','I','F','F',0,0,0,0,'W','A','V','E','J','U','N','K',3,0,0,0,1,2,3,0,
        'f','m','t',' ',16,0,0,0,1,0,1,0,0x22,0x56,0,0,0x22,0x56,0,0,1,0,8,0,'d','a','t','a',0,0,0,0};
    uint32_t total=48+22050,data=22050;
    for(int i=0;i<4;i++){h[4+i]=(unsigned char)(total>>(8*i));h[52+i]=(unsigned char)(data>>(8*i));}
    FILE*f=fopen("/tmp/audio-check.wav","wb");check(f!=NULL,"create wave");
    check(fwrite(h,1,sizeof h,f)==sizeof h,"wave header");
    uint32_t phase=0,increment=(uint32_t)((UINT64_C(500)<<32)/22050);
    for(unsigned i=0;i<22050;++i){int x=(int)(phase>>24);int tri=x<128?x:255-x;
        unsigned char sample=(unsigned char)(128+(tri-64)*40/64);
        check(fwrite(&sample,1,1,f)==1,"wave sample");phase+=increment;}
    check(fclose(f)==0,"wave close");
}
static void play_wave(const char *volume)
{
    fflush(stdout);pid_t child=fork();check(child>=0,"fork player");
    if(!child){char *args[]={"wavplay","-v",(char*)volume,"/tmp/audio-check.wav",NULL};execv("/bin/wavplay",args);_exit(41);}
    int status;check(waitpid(child,&status,0)==child&&status==0,"wavplay child");
}
int main(void)
{
    check(setgid(1000)==0&&setuid(1000)==0,"set console identity");
    int fd=open("/dev/audio",O_WRONLY);
    if(fd<0&&errno==ENODEV){puts("AUDIO_PCM_SKIP_NO_AUDIO");return 0;}
    check(fd>=0,"audio open");
    int duplicate=open("/dev/audio",O_WRONLY);check(duplicate<0&&errno==EBUSY,"exclusive open");
    struct stat st;check(fstat(fd,&st)==0,"audio fstat");
    uint32_t volume=101;check(ioctl(fd,OS_AUDIO_SETVOLUME,&volume)<0&&errno==EINVAL,"invalid volume");
    check(write(fd,buffer,3)<0&&errno==EINVAL,"unaligned PCM");
    volume=80;check(ioctl(fd,OS_AUDIO_SETVOLUME,&volume)==0,"set volume");
    check(fcntl(fd,F_SETFL,O_NONBLOCK)==0,"nonblocking mode");
    uint64_t sent=0;unsigned phase=0;int blocked=0;
    while(sent<48000U*4U*6U){
        size_t bytes=sizeof buffer;if(bytes>48000U*4U*6U-sent)bytes=(size_t)(48000U*4U*6U-sent);
        unsigned local=phase;
        for(size_t i=0;i<bytes/4;i++) {int x=(int)(local%48);int tri=x<24?x:47-x;
            buffer[2*i]=buffer[2*i+1]=(int16_t)((tri-12)*400);++local;}
        ssize_t n=write(fd,buffer,bytes);
        if(n<0&&errno==EAGAIN){
            check(!blocked,"unexpected nonblock retry");blocked=1;
            check(ioctl(fd,OS_AUDIO_DRAIN)<0&&errno==EAGAIN,"nonblocking drain");
            check(fcntl(fd,F_SETFL,0)==0,"blocking mode");continue;
        }
        check(n>0&&n%4==0,"PCM write");sent+=(uint64_t)n;phase+=(unsigned)n/4;
    }
    check(blocked,"queue backpressure exercised");
    check(ioctl(fd,OS_AUDIO_DRAIN)==0,"PCM drain");
    struct os_audio_info info;
    check(ioctl(fd,OS_AUDIO_GETINFO,&info)==0,"get info");
    printf("PCM written=%llu played=%llu queued=%u underruns=%llu flags=%u\n",
        (unsigned long long)info.written_bytes,(unsigned long long)info.played_bytes,
        info.queued_bytes,(unsigned long long)info.underruns,info.flags);
    check(info.written_bytes==sent&&info.played_bytes==sent&&!info.queued_bytes&&
        !(info.flags&OS_AUDIO_ERROR)&&!info.underruns,"continuous stream accounting");
    check(ioctl(fd,OS_AUDIO_RESET)==0,"reset");
    pid_t child=fork();check(child>=0,"fork audio descriptor");
    if(!child){close(fd);_exit(0);}int status;
    check(waitpid(child,&status,0)==child&&status==0,"shared descriptor close");
    check(write(fd,buffer,400)==400&&ioctl(fd,OS_AUDIO_DRAIN)==0,"short stream");
    close(fd);
    fd=open("/dev/audio",O_WRONLY);check(fd>=0,"audio reopen");
    for(int i=0;i<16;i++)check(write(fd,buffer,sizeof buffer)==sizeof buffer,"cancel queue");
    close(fd);
    make_wave();play_wave("50");play_wave("0");
    puts("AUDIO_PCM_PASS user=1000 stream/drain/nonblock/volume/fork/close/reopen/WAV");return 0;
}
