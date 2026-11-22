#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
copy_descriptor(int input)
{
	char buffer[8192];
	ssize_t amount;

	for (;;) {
		amount = read(input, buffer, sizeof(buffer));
		if (amount == 0)
			return 0;
		if (amount < 0) {
			if (errno == EINTR)
				continue;
			return errno;
		}
		if (fwrite(buffer, 1, (size_t)amount, stdout) != (size_t)amount)
			return errno != 0 ? errno : EIO;
	}
}

int
main(int argc, char **argv)
{
	int option, failed = 0;

	opterr = 0;
	while ((option = getopt(argc, argv, "u")) != -1) {
		if (option != 'u') {
			fputs("usage: cat [-u] [file ...]\n", stderr);
			return 2;
		}
		setvbuf(stdout, NULL, _IONBF, 0);
	}
	if (optind == argc) {
		int error = copy_descriptor(STDIN_FILENO);
		if (error != 0) {
			fprintf(stderr, "cat: stdin: %s\n", strerror(error));
			failed = 1;
		}
	}
	for (int i = optind; i < argc; i++) {
		int from_stdin = strcmp(argv[i], "-") == 0;
		int input = from_stdin ? STDIN_FILENO :
		    open(argv[i], O_RDONLY);
		if (input < 0) {
			fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno));
			failed = 1;
			continue;
		}
		int error = copy_descriptor(input);
		if (!from_stdin && close(input) != 0 && error == 0)
			error = errno;
		if (error != 0) {
			fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(error));
			failed = 1;
		}
	}
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror("cat: stdout");
		failed = 1;
	}
	return failed;
}
