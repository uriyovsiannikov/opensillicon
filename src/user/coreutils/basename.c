#include <libgen.h>
#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
	int first = argc > 1 && strcmp(argv[1], "--") == 0 ? 2 : 1;

	if (argc - first < 1 || argc - first > 2) {
		fputs("usage: basename path [suffix]\n", stderr);
		return 2;
	}
	char *name = basename(argv[first]);
	if (argc - first == 2) {
		size_t length = strlen(name), suffix = strlen(argv[first + 1]);
		if (suffix != 0 && suffix < length &&
		    memcmp(name + length - suffix, argv[first + 1], suffix) == 0)
			name[length - suffix] = '\0';
	}
	puts(name);
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror("basename: stdout");
		return 1;
	}
	return 0;
}
