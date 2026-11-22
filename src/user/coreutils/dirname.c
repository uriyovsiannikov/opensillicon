#include <libgen.h>
#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
	int first = argc > 1 && strcmp(argv[1], "--") == 0 ? 2 : 1;

	if (argc - first != 1) {
		fputs("usage: dirname path\n", stderr);
		return 2;
	}
	puts(dirname(argv[first]));
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror("dirname: stdout");
		return 1;
	}
	return 0;
}
