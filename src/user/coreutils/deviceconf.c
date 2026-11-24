#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

#include "deviceconf.h"

int
deviceconf_show(const char *name, const char *node, const char *empty)
{
	char *buffer = malloc(OS_SYSCTL_REPORT_MAX);
	if (buffer == NULL) {
		perror(name);
		return 1;
	}
	size_t amount = OS_SYSCTL_REPORT_MAX;
	int result = sysctlbyname(node, buffer, &amount, NULL, 0);
	if (result == 0) {
		if (amount && buffer[amount - 1] == 0)
			amount--;
		if (amount)
			result = fwrite(buffer, 1, amount, stdout) != amount
				     ? -1
				     : 0;
		else if (empty != NULL)
			result = fputs(empty, stdout) == EOF ? -1 : 0;
		if (fflush(stdout) != 0)
			result = -1;
	}
	if (result != 0)
		fprintf(stderr, "%s: %s: %s\n", name, node, strerror(errno));
	free(buffer);
	return result != 0;
}

int
deviceconf_main(int argc, char **argv, const char *name, const char *node,
    const char *usage, const char *empty)
{
	if (argc > 2 || (argc == 2 && strcmp(argv[1], "-l") != 0)) {
		fprintf(stderr, "usage: %s %s\n", name, usage);
		return 2;
	}
	return deviceconf_show(name, node, empty);
}
