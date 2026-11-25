#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
extern const unsigned char ssh_start[], ssh_end[], keygen_start[], keygen_end[], clientkey_start[], clientkey_end[], sshconfig_start[], sshconfig_end[], knownhosts_start[], knownhosts_end[], scp_start[], scp_end[], destination_start[], destination_end[], badhosts_start[], badhosts_end[], badconfig_start[], badconfig_end[], agent_start[], agent_end[], add_start[], add_end[], server_start[], server_end[];
static void install(const char *path, const unsigned char *start, const unsigned char *end)
{
 FILE *f = fopen(path, "wb");
 if (!f || fwrite(start, 1, end-start, f) != (size_t)(end-start) || fclose(f) || chmod(path, 0755)) { perror(path); exit(1); }
}
static int run(const char *path, char *const argv[])
{
 int status; pid_t pid = fork();
 if (pid == 0) { execv(path, argv); perror("execv"); _exit(127); }
 if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) { puts("SSH_TEST_CHILD_FAILED"); return -1; }
 printf("SSH_TEST_EXIT %s %d\n", argv[0], WEXITSTATUS(status)); fflush(stdout);
 return WEXITSTATUS(status);
}
int main(void)
{
 char *addkey[] = {"ssh-add", "/tmp/id_ed25519", NULL};
 char *listkey[] = {"ssh-add", "-l", NULL};
 char *checkserver[] = {"sshd", "-t", "-e", "-f", "/tmp/sshd_config", NULL};
 char *version[] = {"ssh", "-V", NULL};
 char *generate[] = {"ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", "/tmp/id_ed25519", NULL};
 char *badhost[] = {"ssh", "-F", "/tmp/bad_config", "testhost", "true", NULL};
 char *connect[] = {"ssh", "-F", "/tmp/ssh_config", "testhost", "printf OPENSSH_REMOTE_PASS", NULL};
 char destination[1024];
 char *copy[] = {"scp", "-F", "/tmp/ssh_config", "/tmp/upload", destination, NULL};
 char *fingerprint[] = {"ssh-keygen", "-l", "-f", "/tmp/id_ed25519.pub", NULL};
 mkdir("/usr/bin", 0755);
 install("/tmp/sshd", server_start, server_end);
 install("/tmp/ssh-agent", agent_start, agent_end); install("/tmp/ssh-add", add_start, add_end);
 install("/usr/bin/ssh", ssh_start, ssh_end); install("/tmp/scp", scp_start, scp_end);
 install("/tmp/destination", destination_start, destination_end);
 FILE *file = fopen("/tmp/destination", "r");
 if (!file || !fgets(destination, sizeof destination, file) || fclose(file)) return 3;
 file = fopen("/tmp/upload", "w");
 if (!file || fputs("OpenSillicon SCP transfer\n", file) == EOF || fclose(file)) return 4;
 install("/tmp/ssh", ssh_start, ssh_end); install("/tmp/ssh-keygen", keygen_start, keygen_end);
 install("/tmp/clientkey", clientkey_start, clientkey_end); chmod("/tmp/clientkey", 0600);
 install("/tmp/bad_hosts", badhosts_start, badhosts_end); install("/tmp/bad_config", badconfig_start, badconfig_end);
 install("/tmp/ssh_config", sshconfig_start, sshconfig_end); install("/tmp/known_hosts", knownhosts_start, knownhosts_end);
 if (run("/tmp/ssh", version) || run("/tmp/ssh-keygen", generate) || run("/tmp/ssh-keygen", fingerprint)) return 1;
 mkdir("/var", 0755); mkdir("/var/empty", 0755);
 file = fopen("/etc/passwd", "a");
 if (!file || fputs("sshd:*:74:74:SSH privilege separation:/var/empty:/bin/sh\n", file) == EOF || fclose(file)) return 9;
 file = fopen("/tmp/sshd_config", "w");
 if (!file || fputs("HostKey /tmp/id_ed25519\nPort 2222\n", file) == EOF || fclose(file)) return 10;
 if (run("/tmp/sshd", checkserver)) return 11;
 puts("SSH_SERVER_CONFIG_PASS"); fflush(stdout);
 setenv("SSH_AUTH_SOCK", "/tmp/agent.sock", 1);
 pid_t agent = fork();
 if (agent == 0) { char *args[] = {"ssh-agent", "-D", "-a", "/tmp/agent.sock", NULL}; execv("/tmp/ssh-agent", args); _exit(127); }
 if (agent < 0) return 7;
 sleep(2);
 int agent_status = run("/tmp/ssh-add", addkey) || run("/tmp/ssh-add", listkey);
 kill(agent, SIGTERM);
 waitpid(agent, NULL, 0);
 if (agent_status) return 8;
 puts("SSH_TEST_AGENT_PASS"); fflush(stdout);
 sleep(5);
 if (run("/tmp/ssh", badhost) != 255) return 6;
 puts("SSH_TEST_REJECTED_HOST_KEY"); fflush(stdout);
 if (run("/tmp/ssh", connect)) return 2;
 if (run("/tmp/scp", copy)) return 5;
 puts("OPENSSH_SMOKE_PASS"); fflush(stdout); return 0;
}
