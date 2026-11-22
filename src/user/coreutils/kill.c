#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
parse_number(const char *text, int maximum, int *number)
{
	char *end;

	if (*text < '0' || *text > '9')
		return 0;
	errno = 0;
	intmax_t value = strtoimax(text, &end, 10);
	if (errno != 0 || *end != '\0' || value > maximum)
		return 0;
	*number = (int)value;
	return 1;
}

static int
parse_signal(const char *text, int *number)
{
	if (strncmp(text, "SIG", 3) == 0)
		text += 3;
	if (strcmp(text, "INT") == 0)
		*number = SIGINT;
	else if (strcmp(text, "TERM") == 0)
		*number = SIGTERM;
	else if (strcmp(text, "KILL") == 0)
		*number = SIGKILL;
	else
		return parse_number(text, NSIG - 1, number);
	return 1;
}

int
main(int argc, char **argv)
{
	int signo = SIGTERM, failed = 0, option;

	opterr = 0;
	while ((option = getopt(argc, argv, "s:")) != -1) {
		if (option != 's' || !parse_signal(optarg, &signo))
			goto usage;
	}
	if (optind == argc)
		goto usage;
	for (int i = optind; i < argc; i++) {
		int pid;

		if (!parse_number(argv[i], INT_MAX, &pid) || pid == 0) {
			fprintf(stderr, "kill: invalid pid: %s\n", argv[i]);
			failed = 1;
			continue;
		}
		if (kill((pid_t)pid, signo) < 0) {
			fprintf(stderr, "kill: %s: %s\n", argv[i], strerror(errno));
			failed = 1;
		}
	}
	return failed;
usage:
	fputs("usage: kill [-s INT|TERM|KILL|number] pid ...\n", stderr);
	return 2;
}
