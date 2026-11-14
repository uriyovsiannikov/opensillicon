#ifndef OPENSILLICON_DEVICECONF_H
#define OPENSILLICON_DEVICECONF_H

#include "../include/os.h"

static int
deviceconf_show(uint64_t argc, char **argv, const char *name, const char *path,
	const char *usage)
{
	char buffer[512];
	long fd;
	long amount;
	if (argc > 2 || (argc == 2 && !u_streq(argv[1], "-l"))) {
		u_puts("usage: "); u_puts(name); u_puts(" "); u_puts(usage); u_puts("\n");
		return 1;
	}
	fd = u_open(path, U_O_RDONLY);
	if (fd < 0) {
		u_puts(name); u_puts(": device information unavailable\n");
		return 1;
	}
	while ((amount = u_read((uint64_t)fd, buffer, sizeof(buffer))) > 0) {
		long written = u_write(1, buffer, (size_t)amount);
		if (written != amount) {
			u_close((uint64_t)fd);
			u_puts(name); u_puts(": output failed\n");
			return 1;
		}
	}
	u_close((uint64_t)fd);
	if (amount < 0) {
		u_puts(name); u_puts(": read failed\n");
		return 1;
	}
	return 0;
}

#endif
