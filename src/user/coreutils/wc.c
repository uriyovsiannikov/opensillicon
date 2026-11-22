#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct counts {
	uintmax_t lines;
	uintmax_t words;
	uintmax_t bytes;
};

enum { SHOW_LINES = 1, SHOW_WORDS = 2, SHOW_BYTES = 4 };

static int
count_stream(FILE *input, struct counts *counts)
{
	unsigned char buffer[8192];
	int in_word = 0;
	size_t amount;

	*counts = (struct counts){0};
	while ((amount = fread(buffer, 1, sizeof(buffer), input)) != 0) {
		if (counts->bytes > UINTMAX_MAX - amount)
			return EOVERFLOW;
		counts->bytes += amount;
		/* Both line and word counts are bounded by the checked byte count. */
		for (size_t i = 0; i < amount; i++) {
			if (buffer[i] == '\n')
				counts->lines++;
			if (isspace(buffer[i]))
				in_word = 0;
			else if (!in_word) {
				counts->words++;
				in_word = 1;
			}
		}
	}
	return ferror(input) ? (errno != 0 ? errno : EIO) : 0;
}

static void
print_counts(const struct counts *counts, unsigned show, const char *name)
{
	const uintmax_t values[] = {counts->lines, counts->words, counts->bytes};
	const char *separator = "";

	for (unsigned i = 0; i < 3; i++) {
		if (show & (1U << i)) {
			printf("%s%" PRIuMAX, separator, values[i]);
			separator = " ";
		}
	}
	if (name != NULL)
		printf(" %s", name);
	putchar('\n');
}

int
main(int argc, char **argv)
{
	struct counts total = {0};
	unsigned show = 0;
	int option, failed = 0, overflow = 0;

	opterr = 0;
	while ((option = getopt(argc, argv, "lwc")) != -1) {
		switch (option) {
		case 'l': show |= SHOW_LINES; break;
		case 'w': show |= SHOW_WORDS; break;
		case 'c': show |= SHOW_BYTES; break;
		default:
			fputs("usage: wc [-lwc] [file ...]\n", stderr);
			return 2;
		}
	}
	if (show == 0)
		show = SHOW_LINES | SHOW_WORDS | SHOW_BYTES;
	int files = argc - optind;
	/* The implicit stdin input has no pathname column. */
	int inputs = files == 0 ? 1 : files;
	for (int i = 0; i < inputs; i++) {
		const char *name = files == 0 ? NULL : argv[optind + i];
		FILE *input = name == NULL || strcmp(name, "-") == 0 ? stdin :
		    fopen(name, "rb");
		struct counts counts;

		if (input == NULL) {
			fprintf(stderr, "wc: %s: %s\n", name, strerror(errno));
			failed = 1;
			continue;
		}
		int error = count_stream(input, &counts);
		if (input != stdin && fclose(input) != 0 && error == 0)
			error = errno;
		if (error != 0) {
			fprintf(stderr, "wc: %s: %s\n", name != NULL ? name : "stdin",
			    strerror(error));
			failed = 1;
			continue;
		}
		print_counts(&counts, show, name);
		if (total.bytes > UINTMAX_MAX - counts.bytes) {
			overflow = 1;
		} else {
			total.lines += counts.lines;
			total.words += counts.words;
			total.bytes += counts.bytes;
		}
	}
	if (files > 1 && !overflow)
		print_counts(&total, show, "total");
	if (overflow) {
		fputs("wc: total count overflow\n", stderr);
		failed = 1;
	}
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror("wc: stdout");
		failed = 1;
	}
	return failed;
}
