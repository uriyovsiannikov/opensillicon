#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_VALUE 1000000000000ULL

static int
parse_value(const char *text, uint64_t *value)
{
	char *end;
	if (*text < '0' || *text > '9')
		return 0;
	errno = 0;
	unsigned long long parsed = strtoull(text, &end, 10);
	if (errno != 0 || *end != '\0' || parsed > MAX_VALUE)
		return 0;
	*value = parsed;
	return 1;
}

int
main(int argc, char **argv)
{
	uint64_t first = 1, step = 1, last, value;
	if (argc == 2) {
		if (!parse_value(argv[1], &last))
			goto usage;
	} else if (argc == 3) {
		if (!parse_value(argv[1], &first) ||
		    !parse_value(argv[2], &last))
			goto usage;
	} else if (argc == 4) {
		if (!parse_value(argv[1], &first) ||
		    !parse_value(argv[2], &step) || step == 0 ||
		    !parse_value(argv[3], &last))
			goto usage;
	} else {
		goto usage;
	}
	if (first > last)
		return 0;
	for (value = first;;) {
		if (printf("%" PRIu64 "\n", value) < 0)
			return 1;
		if (last - value < step)
			break;
		value += step;
	}
	return fflush(stdout) == EOF;
usage:
	fputs("usage: seq [first [step]] last\n", stderr);
	return 1;
}
