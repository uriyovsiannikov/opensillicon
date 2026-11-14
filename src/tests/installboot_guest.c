#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static int run(char **argv)
{
 pid_t pid=fork();assert(pid>=0);
 if(pid==0) {execv(argv[0],argv);_exit(127);}
 int status=0;assert(waitpid(pid,&status,0)==pid && WIFEXITED(status));return WEXITSTATUS(status);
}
int main(void)
{
 unsigned char sector[512];int fd=open("/dev/ada0p1",O_RDONLY);assert(fd>=0);
 assert(read(fd,sector,sizeof(sector))==sizeof(sector) && sector[0]==0xcc);
 char *preview[]={"/bin/installboot","-N","-r","00112233-4455-4677-8899-aabbccddeeff","/dev/ada0p1",NULL};
 assert(run(preview)==0);
 assert(lseek(fd,0,SEEK_SET)==0 && read(fd,sector,sizeof(sector))==sizeof(sector) && sector[0]==0xcc);
 char *format[]={"/bin/installboot","-F","-r","00112233-4455-4677-8899-aabbccddeeff","/dev/ada0p1",NULL};
 assert(run(format)==0);
 assert(lseek(fd,0,SEEK_SET)==0 && read(fd,sector,sizeof(sector))==sizeof(sector) && sector[510]==0x55 && sector[511]==0xaa);
 close(fd);puts("INSTALLBOOT GUEST PASS: preview and native UEFI installation");fflush(stdout);
 for(;;)sleep(1);
}
