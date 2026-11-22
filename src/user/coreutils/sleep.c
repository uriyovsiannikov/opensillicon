#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static int
parse_seconds(const char *text, int64_t *seconds)
{
	uint64_t value = 0;
	if (*text == '\0')
		return 0;
	while (*text != '\0') {
		if (*text < '0' || *text > '9')
			return 0;
		unsigned digit = (unsigned)(*text++ - '0');
		if (value > ((uint64_t)INT64_MAX - digit) / 10)
			return 0;
		value = value * 10 + digit;
	}
	*seconds = (int64_t)value;
	return 1;
}

int
main(int argc, char **argv)
{
	int64_t seconds;
	if (argc != 2 || !parse_seconds(argv[1], &seconds)) {
		fputs("usage: sleep whole-seconds\n", stderr);
		return 2;
	}
	struct timespec request = {.tv_sec = seconds};
	while (nanosleep(&request, &request) < 0) {
		if (errno != EINTR) {
			perror("sleep");
			return 1;
		}
	}
	return 0;
}
