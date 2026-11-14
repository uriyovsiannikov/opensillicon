#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool
trusted_path(const char *path, bool directory)
{
	struct stat st;
	return stat(path, &st) == 0 && st.st_uid == 0 &&
	    (st.st_mode & 022) == 0 &&
	    (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}

static bool
resolve_command(const char *command, char path[256])
{
	static const char *const directories[] = { "/bin", "/usr/bin", "/sbin" };
	if (strchr(command, '/') != NULL) {
		char *resolved = realpath(command, NULL);
		if (resolved == NULL)
			return false;
		size_t length = strlen(resolved);
		if (length < 256)
			memcpy(path, resolved, length + 1);
		free(resolved);
		if (length >= 256)
			return false;
	} else {
		bool found = false;
		for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
			int n = snprintf(path, 256, "%s/%s", directories[i], command);
			if (n > 0 && n < 256 && trusted_path(path, false)) {
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	char parent[256];
	strcpy(parent, path);
	for (char *p = parent + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = 0;
		bool trusted = trusted_path(parent, true);
		*p = '/';
		if (!trusted)
			return false;
	}
	struct stat st;
	return trusted_path(path, false) && stat(path, &st) == 0 &&
	    (st.st_mode & 0111) != 0;
}

static int
authorize(const char *path, char *const arguments[])
{
	static char config[4097];
	struct stat st;
	int fd = open("/etc/doas.conf", O_RDONLY | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	if (!trusted_path("/etc", true) || fstat(fd, &st) < 0 ||
	    !S_ISREG(st.st_mode) || st.st_uid != 0 || (st.st_mode & 022) != 0) {
		close(fd);
		return -1;
	}
	size_t used = 0;
	ssize_t amount;
	while (used < sizeof(config) &&
	    (amount = read(fd, config + used, sizeof(config) - used)) > 0)
		used += (size_t)amount;
	close(fd);
	if (amount < 0 || used == sizeof(config) || memchr(config, 0, used))
		return -1;
	config[used] = 0;

	char numeric[16];
	snprintf(numeric, sizeof(numeric), "%u", (unsigned)getuid());
	struct passwd *caller = getpwuid(getuid());
	bool allowed = false;
	for (char *line = config; *line;) {
		char *next = strchr(line, '\n');
		if (next != NULL)
			*next++ = 0;
		else
			next = line + strlen(line);
		char *comment = strchr(line, '#');
		if (comment != NULL)
			*comment = 0;
		char *words[24];
		size_t count = 0;
		while (*line) {
			line += strspn(line, " \t\r");
			if (*line == 0)
				break;
			if (count == sizeof(words) / sizeof(words[0]))
				return -1;
			words[count++] = line;
			line += strcspn(line, " \t\r");
			if (*line)
				*line++ = 0;
		}
		line = next;
		if (count == 0)
			continue;
		bool permit = strcmp(words[0], "permit") == 0;
		size_t identity = permit ? 2 : 1;
		size_t command = identity + 4;
		if ((!permit && strcmp(words[0], "deny") != 0) ||
		    (permit && (count < 2 || strcmp(words[1], "nopass") != 0)) ||
		    count <= command || strcmp(words[identity + 1], "as") != 0 ||
		    (strcmp(words[identity + 2], "root") != 0 &&
		    strcmp(words[identity + 2], "0") != 0) ||
		    strcmp(words[identity + 3], "cmd") != 0 ||
		    words[command][0] != '/' ||
		    (count > command + 1 && strcmp(words[command + 1], "args") != 0))
			return -1;
		bool match = (strcmp(words[identity], numeric) == 0 ||
		    (caller != NULL && strcmp(words[identity], caller->pw_name) == 0)) &&
		    strcmp(words[command], path) == 0;
		if (count > command + 1) {
			size_t i = 0;
			while (arguments[i + 1] != NULL && command + 2 + i < count &&
			    strcmp(arguments[i + 1], words[command + 2 + i]) == 0)
				i++;
			match = match && arguments[i + 1] == NULL &&
			    command + 2 + i == count;
		}
		if (match)
			allowed = permit;
	}
	return allowed;
}

int
main(int argc, char **argv)
{
	int arg = 1;
	if (arg < argc && strcmp(argv[arg], "-n") == 0)
		arg++;
	if (arg < argc && strcmp(argv[arg], "--") == 0)
		arg++;
	if (arg == argc || argv[arg][0] == '-') {
		fputs("usage: doas [-n] command [argument ...]\n", stderr);
		return 2;
	}
	char path[256];
	if (geteuid() != 0) {
		fputs("doas: must be installed setuid root\n", stderr);
		return 1;
	}
	if (!resolve_command(argv[arg], path)) {
		fputs("doas: command is unavailable or its path is writable by other users\n", stderr);
		return 1;
	}
	int allowed = authorize(path, argv + arg);
	if (allowed <= 0) {
		fputs(allowed < 0 ? "doas: invalid /etc/doas.conf\n" :
		    "doas: operation not permitted\n", stderr);
		return 1;
	}
	for (int fd = 3; fd < 32; fd++)
		close(fd);
	umask(022);
	if (setresgid(0, 0, 0) < 0 || setresuid(0, 0, 0) < 0) {
		perror("doas: credentials");
		return 1;
	}
	execv(path, argv + arg);
	perror("doas: exec");
	return 1;
}
