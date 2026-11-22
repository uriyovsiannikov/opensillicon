#include <errno.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int
is_executable(const char *path)
{
	struct stat status;

	return stat(path, &status) == 0 && S_ISREG(status.st_mode) &&
	    access(path, X_OK) == 0;
}

static int
find_command(const char *name, const char *path)
{
	size_t name_length = strlen(name);

	if (name_length == 0)
		return 0;
	if (strchr(name, '/') != NULL) {
		if (!is_executable(name))
			return 0;
		puts(name);
		return 1;
	}
	for (;;) {
		const char *end = strchr(path, ':');
		size_t length = end != NULL ? (size_t)(end - path) : strlen(path);

		if (length > SIZE_MAX - name_length - 2) {
			errno = ENOMEM;
			return -1;
		}
		char *candidate = malloc(length + name_length + 2);
		if (candidate == NULL)
			return -1;
		memcpy(candidate, path, length);
		if (length != 0)
			candidate[length++] = '/';
		memcpy(candidate + length, name, name_length + 1);
		int found = is_executable(candidate);
		if (found)
			puts(candidate);
		free(candidate);
		if (found)
			return 1;
		if (end == NULL)
			return 0;
		path = end + 1;
	}
}

int
main(int argc, char **argv)
{
	int failed = 0;

	opterr = 0;
	if (getopt(argc, argv, "") != -1 || optind == argc) {
		fputs("usage: which command ...\n", stderr);
		return 2;
	}
	const char *path = getenv("PATH");
	if (path == NULL)
		path = _PATH_DEFPATH;
	for (int i = optind; i < argc; i++) {
		int found = find_command(argv[i], path);
		if (found < 0)
			perror("which");
		if (found <= 0)
			failed = 1;
	}
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror("which: stdout");
		failed = 1;
	}
	return failed;
}
