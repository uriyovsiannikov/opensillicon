#include <errno.h>
#include <stdio.h>
#include <unistd.h>
int
main(int argc, char **argv)
{
	(void)argv;
	if (argc != 1) {
		fputs("usage: sync\n", stderr);
		return 2;
	}
	if (sync_all() != 0) {
		fprintf(stderr, "sync: failed (errno %d)\n", errno);
		return 1;
	}
	return 0;
}
