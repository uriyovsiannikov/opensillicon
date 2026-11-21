#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wctype.h>
int lstat(const char *name, struct stat *st) {
  return fstatat(AT_FDCWD, name, st, AT_SYMLINK_NOFOLLOW);
}
int setreuid(uid_t real, uid_t effective) {
  return setresuid(real, effective, (uid_t)-1);
}
int iswspace(wint_t c) {
  return c == 0x20 || (c >= 9 && c <= 13) || c == 0x85 || c == 0xa0 ||
         c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
         c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
static FILE *passwd_file;
void endpwent(void) {
  if (passwd_file)
    fclose(passwd_file);
  passwd_file = NULL;
}
void setpwent(void) {
  endpwent();
  passwd_file = fopen("/etc/passwd", "r");
}
struct passwd *getpwent(void) {
  static struct passwd pw;
  static char line[1024];
  if (!passwd_file)
    setpwent();
  while (passwd_file && fgets(line, sizeof line, passwd_file)) {
    char *fields[7], *p = line;
    unsigned i;
    for (i = 0; i < 7; i++) {
      fields[i] = p;
      char *sep = strchr(p, i == 6 ? '\n' : ':');
      if (!sep && i != 6)
        break;
      if (sep) {
        *sep = 0;
        p = sep + 1;
      }
    }
    if (i < 7)
      continue;
    pw.pw_name = fields[0];
    pw.pw_passwd = fields[1];
    pw.pw_uid = strtoul(fields[2], NULL, 10);
    pw.pw_gid = strtoul(fields[3], NULL, 10);
    pw.pw_gecos = fields[4];
    pw.pw_dir = fields[5];
    pw.pw_shell = fields[6];
    return &pw;
  }
  return NULL;
}
#include <netdb.h>
#include <netinet/in.h>
struct hostent *gethostbyname(const char *name) {
  static struct hostent host;
  static struct in_addr address;
  static char *list[2], *aliases[1];
  struct addrinfo hints = {0}, *result;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(name, NULL, &hints, &result))
    return NULL;
  address = ((struct sockaddr_in *)result->ai_addr)->sin_addr;
  list[0] = (char *)&address;
  list[1] = NULL;
  host.h_name = (char *)name;
  host.h_aliases = aliases;
  host.h_addrtype = AF_INET;
  host.h_length = sizeof(address);
  host.h_addr_list = list;
  freeaddrinfo(result);
  return &host;
}
/* libos has no truncate syscall. Refuse ID3 deletion rather than corrupting a
 * file. */
int ftruncate(int fd, off_t length) {
  (void)fd;
  (void)length;
  errno = ENOSYS;
  return -1;
}
