#include <signal.h>
#include <stdio.h>

static unsigned first_count, second_count, order;

__attribute__((constructor(101))) static void
first(void)
{
	first_count++;
	order = order * 10 + 1;
}

__attribute__((constructor(102))) static void
second(void)
{
	second_count++;
	order = order * 10 + 2;
}

int
main(void)
{
	struct sigaction action;

	if (first_count != 1 || second_count != 1 || order != 12) {
		fprintf(stderr, "constructor counts: %u %u order %u\n",
		    first_count, second_count, order);
		return 1;
	}
	if (sigaction(SIGPIPE, NULL, &action) != 0 || action.sa_handler != SIG_IGN)
		return 2;
	puts("NETSURF_RUNTIME_PASS");
	return 0;
}
