#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *
type_name(mode_t mode)
{
	if (S_ISREG(mode)) return "regular file";
	if (S_ISDIR(mode)) return "directory";
	if (S_ISLNK(mode)) return "symbolic link";
	if (S_ISCHR(mode)) return "character device";
	if (S_ISBLK(mode)) return "block device";
	if (S_ISFIFO(mode)) return "fifo";
	if (S_ISSOCK(mode)) return "socket";
	return "unknown";
}

int
main(int argc, char **argv)
{
	int follow = 0, option, failed = 0;

	opterr = 0;
	while ((option = getopt(argc, argv, "L")) != -1) {
		if (option != 'L')
			goto usage;
		follow = 1;
	}
	if (optind == argc)
		goto usage;
	for (int i = optind; i < argc; i++) {
		struct stat status;
		int result = follow ? stat(argv[i], &status) : lstat(argv[i], &status);

		if (result < 0) {
			fprintf(stderr, "stat: cannot stat: %s: %s\n", argv[i],
			    strerror(errno));
			failed = 1;
			continue;
		}
		printf("  File: %s\n  Type: %s\n  Size: %" PRIdMAX
		    " bytes\n  Mode: %o\n", argv[i], type_name(status.st_mode),
		    (intmax_t)status.st_size, (unsigned)(status.st_mode & 07777));
	}
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror("stat: stdout");
		failed = 1;
	}
	return failed;
usage:
	fputs("usage: stat [-L] file ...\n", stderr);
	return 2;
}
