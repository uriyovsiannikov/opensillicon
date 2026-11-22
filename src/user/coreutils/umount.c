#include <errno.h>
#include <stdio.h>
#include <sys/mount.h>
int
main(int argc, char **argv)
{
	if (argc != 2) {
		fputs("usage: umount /mnt\n", stderr);
		return 2;
	}
	if (umount2(argv[1], 0) != 0) {
		fprintf(stderr, "umount: failed (errno %d)\n", errno);
		return 1;
	}
	return 0;
}
