#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
head_stream(FILE *input, uintmax_t lines)
{
	int character;

	while (lines != 0 && (character = getc(input)) != EOF) {
		if (putc(character, stdout) == EOF)
			return errno != 0 ? errno : EIO;
		if (character == '\n')
			lines--;
	}
	return ferror(input) ? (errno != 0 ? errno : EIO) : 0;
}

int
main(int argc, char **argv)
{
	uintmax_t lines = 10;
	int option, failed = 0, have_output = 0;

	opterr = 0;
	while ((option = getopt(argc, argv, "n:")) != -1) {
		char *end;

		if (option != 'n' || *optarg < '0' || *optarg > '9')
			goto usage;
		errno = 0;
		lines = strtoumax(optarg, &end, 10);
		if (errno != 0 || *end != '\0')
			goto usage;
	}
	int files = argc - optind;
	if (files == 0) {
		int error = head_stream(stdin, lines);
		if (error != 0) {
			fprintf(stderr, "head: stdin: %s\n", strerror(error));
			failed = 1;
		}
	}
	for (int i = optind; i < argc; i++) {
		FILE *input = strcmp(argv[i], "-") == 0 ? stdin :
		    fopen(argv[i], "rb");
		if (input == NULL) {
			fprintf(stderr, "head: %s: %s\n", argv[i], strerror(errno));
			failed = 1;
			continue;
		}
		if (files > 1) {
			if (have_output)
				putchar('\n');
			printf("==> %s <==\n", argv[i]);
			have_output = 1;
		}
		int error = head_stream(input, lines);
		if (input != stdin && fclose(input) != 0 && error == 0)
			error = errno;
		if (error != 0) {
			fprintf(stderr, "head: %s: %s\n", argv[i], strerror(error));
			failed = 1;
		}
	}
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror("head: stdout");
		failed = 1;
	}
	return failed;
usage:
	fputs("usage: head [-n lines] [file ...]\n", stderr);
	return 2;
}
