#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "deviceconf.h"

int
deviceconf_show(const char *name, const char *path, const char *empty)
{
	FILE *input = fopen(path, "rb");
	char buffer[4096];
	size_t amount;
	int error = 0, seen = 0;

	if (input == NULL) {
		fprintf(stderr, "%s: %s: %s\n", name, path, strerror(errno));
		return 1;
	}
	while ((amount = fread(buffer, 1, sizeof(buffer), input)) != 0) {
		seen = 1;
		if (fwrite(buffer, 1, amount, stdout) != amount) {
			error = errno != 0 ? errno : EIO;
			break;
		}
	}
	if (error == 0 && ferror(input))
		error = errno != 0 ? errno : EIO;
	if (fclose(input) != 0 && error == 0)
		error = errno;
	if (error == 0 && !seen && empty != NULL)
		fputs(empty, stdout);
	if ((fflush(stdout) != 0 || ferror(stdout)) && error == 0)
		error = errno != 0 ? errno : EIO;
	if (error != 0)
		fprintf(stderr, "%s: %s: %s\n", name, path, strerror(error));
	return error != 0;
}

int
deviceconf_main(int argc, char **argv, const char *name, const char *path,
    const char *usage, const char *empty)
{
	if (argc > 2 || (argc == 2 && strcmp(argv[1], "-l") != 0)) {
		fprintf(stderr, "usage: %s %s\n", name, usage);
		return 2;
	}
	return deviceconf_show(name, path, empty);
}
