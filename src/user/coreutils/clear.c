#include <stdio.h>

int
main(int argc, char **argv)
{
	static const char sequence[] = "\033[0m\033[2J\033[H";
	(void)argv;
	if (argc != 1) {
		fputs("clear: arguments are unsupported\n", stderr);
		return 1;
	}
	return fwrite(sequence, 1, sizeof(sequence) - 1, stdout) !=
	    sizeof(sequence) - 1 || fflush(stdout) == EOF;
}
