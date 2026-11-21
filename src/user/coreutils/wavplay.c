#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "abi/audio.h"

struct wave { unsigned rate, channels, bits, align; uint32_t bytes; long offset; };
static unsigned le16(const unsigned char *p) {return p[0]|((unsigned)p[1]<<8);}
static uint32_t le32(const unsigned char *p) {return le16(p)|((uint32_t)le16(p+2)<<16);}
static int parse(FILE *f,struct wave *w)
{
    unsigned char header[16];
    if(fseek(f,0,SEEK_END))return -1;
    long size=ftell(f);if(size<12||fseek(f,0,SEEK_SET)||fread(header,1,12,f)!=12)return -1;
    if(memcmp(header,"RIFF",4)||memcmp(header+8,"WAVE",4))return -1;
    uint64_t end=(uint64_t)le32(header+4)+8;if(end>(uint64_t)size||end<12)return -1;
    int have_format=0,have_data=0;
    for(uint64_t off=12;off+8<=end;){
        if(fseek(f,(long)off,SEEK_SET)||fread(header,1,8,f)!=8)return -1;
        uint32_t n=le32(header+4);uint64_t next=off+8+(uint64_t)n+(n&1U);
        if(next>end)return -1;
        if(!memcmp(header,"fmt ",4)&&!have_format){
            if(n<16||fread(header,1,16,f)!=16||le16(header)!=1)return -1;
            w->channels=le16(header+2);w->rate=le32(header+4);
            w->align=le16(header+12);w->bits=le16(header+14);
            if((w->channels!=1&&w->channels!=2)||(w->bits!=8&&w->bits!=16)||
                w->rate<8000||w->rate>192000||w->align!=w->channels*(w->bits/8)||
                le32(header+8)!=w->rate*w->align)return -1;
            have_format=1;
        } else if(!memcmp(header,"data",4)&&!have_data){w->offset=(long)(off+8);w->bytes=n;have_data=1;}
        off=next;
    }
    return have_format&&have_data&&w->bytes&&w->bytes%w->align==0?0:-1;
}
static int frame(FILE *f,const struct wave *w,int sample[2])
{
    unsigned char data[4];if(fread(data,1,w->align,f)!=w->align)return -1;
    for(unsigned c=0;c<w->channels;++c){
        if(w->bits==8)sample[c]=((int)data[c]-128)*256;
        else {unsigned v=le16(data+2*c);sample[c]=(int)(v&32767)-(int)(v&32768);}
    }
    if(w->channels==1)sample[1]=sample[0];
    return 0;
}
static int output(int fd,const void *data,size_t bytes)
{
    const unsigned char *p=data;
    while(bytes){ssize_t n=write(fd,p,bytes);if(n<0&&errno==EINTR)continue;
        if(n<=0)return -1;
        p+=n;bytes-=(size_t)n;}
    return 0;
}
int main(int argc,char **argv)
{
    unsigned volume=70;int arg=1;
    if(argc>3&&!strcmp(argv[1],"-v")){
        char *end;unsigned long n=strtoul(argv[2],&end,10);
        if(!*argv[2]||*end||n>100){fputs("wavplay: volume must be 0..100\n",stderr);return 2;}
        volume=(unsigned)n;arg=3;
    }
    if(argc!=arg+1){fputs("usage: wavplay [-v 0..100] file.wav\n",stderr);return 2;}
    FILE *file=fopen(argv[arg],"rb");if(!file){perror("wavplay: open WAV");return 1;}
    struct wave w={0};
    if(parse(file,&w)){fputs("wavplay: malformed/unsupported WAV; use PCM 8/16-bit mono/stereo, 8000..192000 Hz\n",stderr);fclose(file);return 1;}
    if(fseek(file,w.offset,SEEK_SET)){perror("wavplay: seek");fclose(file);return 1;}
    int fd=open("/dev/audio",O_WRONLY);
    if(fd<0){perror("wavplay: /dev/audio");fclose(file);return 1;}
    int result=1;
    if(ioctl(fd,OS_AUDIO_SETVOLUME,&volume)){perror("wavplay: volume");goto done;}
    printf("Playing %u Hz, %u channel(s), %u-bit PCM at volume %u%%\n",w.rate,w.channels,w.bits,volume);
    uint64_t frames=w.bytes/w.align, outputs=(frames*48000+w.rate-1)/w.rate, next=1;
    unsigned phase=0;int a[2],b[2];int16_t buffer[2048];size_t used=0;
    if(frame(file,&w,a))goto read_error;
    b[0]=a[0];b[1]=a[1];
    if(next<frames){if(frame(file,&w,b))goto read_error;++next;}
    for(uint64_t i=0;i<outputs;++i){
        for(unsigned c=0;c<2;++c)
            buffer[used++]=(int16_t)(((int64_t)a[c]*(48000-phase)+(int64_t)b[c]*phase)/48000);
        if(used==2048){if(output(fd,buffer,sizeof(buffer))){perror("wavplay: audio write");goto done;}used=0;}
        phase+=w.rate;
        while(phase>=48000){phase-=48000;a[0]=b[0];a[1]=b[1];
            if(next<frames){if(frame(file,&w,b))goto read_error;++next;}}
    }
    if(used&&output(fd,buffer,used*sizeof(buffer[0]))){perror("wavplay: audio write");goto done;}
    while(ioctl(fd,OS_AUDIO_DRAIN)){if(errno==EINTR)continue;perror("wavplay: audio drain");goto done;}
    struct os_audio_info info;
    if(ioctl(fd,OS_AUDIO_GETINFO,&info)){perror("wavplay: audio status");goto done;}
    printf("Played %llu PCM bytes; underrun periods: %llu\n",
        (unsigned long long)info.played_bytes,(unsigned long long)info.underruns);
    result=0;goto done;
read_error:
    fputs("wavplay: truncated PCM data\n",stderr);
done:
    close(fd);fclose(file);return result;
}
