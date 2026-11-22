#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int
logical_path(const char *path)
{
	if (path == NULL || path[0] != '/')
		return 0;
	while (*path != '\0') {
		path += strspn(path, "/");
		size_t length = strcspn(path, "/");
		if ((length == 1 && path[0] == '.') ||
		    (length == 2 && path[0] == '.' && path[1] == '.'))
			return 0;
		path += length;
	}
	return 1;
}

int
main(int argc, char **argv)
{
	int logical = 1, option;

	opterr = 0;
	while ((option = getopt(argc, argv, "LP")) != -1) {
		if (option != 'L' && option != 'P')
			goto usage;
		logical = option == 'L';
	}
	if (optind != argc)
		goto usage;
	const char *path = logical ? getenv("PWD") : NULL;
	struct stat current, supplied;
	/* An inherited logical pathname must actually name our working directory. */
	if (logical_path(path) && stat(".", &current) == 0 &&
	    stat(path, &supplied) == 0 && current.st_dev == supplied.st_dev &&
	    current.st_ino == supplied.st_ino) {
		puts(path);
	} else {
		char *physical = getcwd(NULL, 0);
		if (physical == NULL) {
			perror("pwd: getcwd");
			return 1;
		}
		puts(physical);
		free(physical);
	}
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror("pwd: stdout");
		return 1;
	}
	return 0;
usage:
	fputs("usage: pwd [-L | -P]\n", stderr);
	return 2;
}
