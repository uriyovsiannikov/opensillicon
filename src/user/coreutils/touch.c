#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int failed = 0;
	if (argc < 2) {
		fputs("usage: touch file ...\n", stderr);
		return 1;
	}
	for (int i = 1; i < argc; i++) {
		int fd = open(argv[i], O_WRONLY | O_CREAT, 0666);
		if (fd < 0) {
			fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno));
			failed = 1;
		} else if (close(fd) < 0) {
			fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno));
			failed = 1;
		}
	}
	return failed;
}
