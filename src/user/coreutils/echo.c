#include <stdio.h>

int
main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (i != 1 && putchar(' ') == EOF)
			return 1;
		if (fputs(argv[i], stdout) == EOF)
			return 1;
	}
	if (putchar('\n') == EOF)
		return 1;
	return fflush(stdout) == EOF;
}
