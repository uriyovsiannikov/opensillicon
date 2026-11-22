#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct line {
	struct line *next;
	char *text;
	size_t length;
};

static int
parse_count(const char *text, uintmax_t *count)
{
	char *end;

	if (*text < '0' || *text > '9')
		return 0;
	errno = 0;
	*count = strtoumax(text, &end, 10);
	return errno == 0 && *end == '\0';
}

static int
tail_stream(FILE *input, uintmax_t limit)
{
	struct line *first = NULL, *last = NULL;
	uintmax_t count = 0;
	char *text = NULL;
	size_t capacity = 0;
	ssize_t length;
	int error = 0;

	if (limit == 0)
		return 0;
	while ((length = getline(&text, &capacity, input)) >= 0) {
		struct line *line = malloc(sizeof(*line));

		if (line == NULL) {
			error = ENOMEM;
			break;
		}
		*line = (struct line){ .text = text, .length = (size_t)length };
		text = NULL;
		capacity = 0;
		if (count == limit) {
			struct line *old = first;
			first = old->next;
			if (first == NULL)
				last = NULL;
			free(old->text);
			free(old);
		} else {
			count++;
		}
		if (last != NULL)
			last->next = line;
		else
			first = line;
		last = line;
	}
	if (error == 0 && !feof(input))
		error = errno != 0 ? errno : EIO;
	free(text);
	while (first != NULL) {
		struct line *line = first;

		first = line->next;
		if (error == 0 && fwrite(line->text, 1, line->length, stdout) !=
		    line->length)
			error = errno != 0 ? errno : EIO;
		free(line->text);
		free(line);
	}
	return error;
}

int
main(int argc, char **argv)
{
	uintmax_t lines = 10;
	int option, failed = 0;
	int have_output = 0;

	opterr = 0;
	while ((option = getopt(argc, argv, "n:")) != -1) {
		if (option != 'n' || !parse_count(optarg, &lines)) {
			fputs("usage: tail [-n lines] [file ...]\n", stderr);
			return 2;
		}
	}
	int files = argc - optind;
	if (files == 0) {
		int error = tail_stream(stdin, lines);
		if (error != 0) {
			fprintf(stderr, "tail: stdin: %s\n", strerror(error));
			failed = 1;
		}
	}
	for (int i = optind; i < argc; i++) {
		FILE *input = strcmp(argv[i], "-") == 0 ? stdin :
		    fopen(argv[i], "rb");
		if (input == NULL) {
			fprintf(stderr, "tail: %s: %s\n", argv[i], strerror(errno));
			failed = 1;
			continue;
		}
		if (files > 1) {
			if (have_output)
				putchar('\n');
			printf("==> %s <==\n", argv[i]);
			have_output = 1;
		}
		int error = tail_stream(input, lines);
		if (input != stdin && fclose(input) != 0 && error == 0)
			error = errno;
		if (error != 0) {
			fprintf(stderr, "tail: %s: %s\n", argv[i], strerror(error));
			failed = 1;
		}
	}
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror("tail: stdout");
		failed = 1;
	}
	return failed;
}
