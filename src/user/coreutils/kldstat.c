#include <stdio.h>

#include "deviceconf.h"

int
main(int argc, char **argv)
{
	(void)argv;
	if (argc != 1) {
		fputs("usage: kldstat\n", stderr);
		return 2;
	}
	return deviceconf_show("kldstat", "/proc/modules", NULL);
}
