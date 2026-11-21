#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static struct passwd root_user = {
	.pw_name = "root", .pw_passwd = "", .pw_uid = 0, .pw_gid = 0,
	.pw_gecos = "root", .pw_dir = "/", .pw_shell = "/bin/sh"
};

/* Match POSIX's static result lifetime. Do not share the enumeration cursor. */
static int fields(char *line, char **out, unsigned count)
{
	for (unsigned i = 0; i < count; i++) {
		out[i] = line;
		char *end = strchr(line, i + 1 == count ? '\n' : ':');
		if (!end && i + 1 != count) return 0;
		if (end) { *end = 0; line = end + 1; }
	}
	return 1;
}
static int number(const char *s, unsigned int *value)
{
	char *end;
	if (!*s || *s < '0' || *s > '9') return 0;
	unsigned long n = strtoul(s, &end, 10);
	if (*end || n > UINT32_MAX) return 0;
	*value = n; return 1;
}
static struct passwd *lookup(const char *name, uid_t uid)
{
	static struct passwd result;
	static char line[1024];
	FILE *file = fopen("/etc/passwd", "r");
	if (!file) {
		if ((name && !strcmp(name, "root")) || (!name && uid == 0)) return &root_user;
		return NULL;
	}
	while (fgets(line, sizeof line, file)) {
		char *part[7]; unsigned int user, group;
		/* Discard an oversized record instead of interpreting its tail. */
		if (!strchr(line, '\n') && !feof(file)) {
			int c; while ((c = fgetc(file)) != '\n' && c != EOF) {} continue;
		}
		if (!fields(line, part, 7) || !number(part[2], &user) || !number(part[3], &group)) continue;
		if (name ? strcmp(name, part[0]) != 0 : uid != user) continue;
		result = (struct passwd){part[0],part[1],user,group,part[4],part[5],part[6]};
		fclose(file); return &result;
	}
	fclose(file); errno = ENOENT; return NULL;
}
struct passwd *getpwuid(uid_t uid) { return lookup(NULL, uid); }
struct passwd *getpwnam(const char *name)
{
	if (!name) { errno = EINVAL; return NULL; }
	return lookup(name, 0);
}
struct group *getgrnam(const char *name)
{
	static struct group result;
	static char line[1024];
	static char *members[64];
	if (!name) { errno = EINVAL; return NULL; }
	FILE *file = fopen("/etc/group", "r");
	if (!file) return NULL;
	while (fgets(line, sizeof line, file)) {
		char *part[4]; unsigned int gid;
		if (!strchr(line, '\n') && !feof(file)) {
			int c; while ((c = fgetc(file)) != '\n' && c != EOF) {} continue;
		}
		if (!fields(line, part, 4) || strcmp(name,part[0]) || !number(part[2],&gid)) continue;
		unsigned n = 0; char *p = part[3];
		while (*p && n < 63) {
			members[n++] = p; char *comma = strchr(p, ',');
			if (!comma) break;
			*comma = 0; p = comma + 1;
		}
		members[n] = NULL;
		result = (struct group){part[0],part[1],gid,members};
		fclose(file); return &result;
	}
	fclose(file); errno = ENOENT; return NULL;
}
