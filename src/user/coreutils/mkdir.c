#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int failed = 0;

	opterr = 0;
	if (getopt(argc, argv, "") != -1 || optind == argc) {
		fputs("usage: mkdir directory ...\n", stderr);
		return 2;
	}
	for (int i = optind; i < argc; i++) {
		if (mkdir(argv[i], 0777) < 0) {
			fprintf(stderr, "mkdir: cannot create: %s: %s\n", argv[i],
			    strerror(errno));
			failed = 1;
		}
	}
	return failed;
}
