/* Insecure, isolated QEMU fixture: run sshd as an unprivileged test user. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
extern const unsigned char daemon_start[], daemon_end[], session_start[], session_end[], auth_start[], auth_end[], hostkey_start[], hostkey_end[], authorized_start[], authorized_end[], echo_start[], echo_end[];
static void put(const char *path, const unsigned char *a, const unsigned char *b, int mode)
{
 FILE *f=fopen(path,"wb");
 if (!f || fwrite(a,1,b-a,f)!=(size_t)(b-a) || fclose(f) || chmod(path,mode)) { perror(path); exit(1); }
}
int main(void)
{
 mkdir("/usr/libexec",0755);
 put("/tmp/sshd",daemon_start,daemon_end,0755);
 put("/bin/echo",echo_start,echo_end,0755);
 put("/usr/libexec/sshd-session",session_start,session_end,0755);
 put("/usr/libexec/sshd-auth",auth_start,auth_end,0755);
 put("/tmp/ssh_host_key",hostkey_start,hostkey_end,0600);
 if (chown("/tmp/ssh_host_key",1000,1000)) { perror("chown"); return 1; }
 put("/tmp/authorized_keys",authorized_start,authorized_end,0644);
 FILE *f=fopen("/etc/passwd","w");
 if (!f || fputs("root:*:0:0:root:/:/bin/sh\nuser::1000:1000:user:/tmp:/bin/sh\n",f)==EOF || fclose(f)) return 1;
 f=fopen("/tmp/sshd_config","w");
 if (!f || fputs("Port 2222\nLoginGraceTime 0\nListenAddress 0.0.0.0\nHostKey /tmp/ssh_host_key\nPidFile /tmp/sshd.pid\nAuthorizedKeysFile /tmp/authorized_keys\nStrictModes no\nPasswordAuthentication no\nKbdInteractiveAuthentication no\nUseDNS no\nLogLevel DEBUG3\n",f)==EOF || fclose(f)) return 1;
 if (setgid(1000) || setuid(1000)) { perror("setuid/setgid"); return 1; }
 puts("SSH_SERVER_START"); fflush(stdout);
 char *args[]={"/tmp/sshd","-D","-e","-f","/tmp/sshd_config",NULL};
 execv("/tmp/sshd",args); perror("execv sshd"); return 1;
}
