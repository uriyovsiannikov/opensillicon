#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	char buffer[512];
	long amount;
	int seen = 0;
	if (argc > 2 || (argc == 2 && !u_streq(argv[1], "-l"))) {
		u_puts("usage: netconf [-l]\n");
		return 1;
	}
	long fd = u_open("/proc/netpci", U_O_RDONLY);
	if (fd < 0) {
		u_puts("netconf: PCI network information unavailable\n");
		return 1;
	}
	while ((amount = u_read((uint64_t)fd, buffer, sizeof(buffer))) > 0) {
		seen = 1;
		if (u_write(1, buffer, (size_t)amount) != amount) {
			u_close((uint64_t)fd);
			return 1;
		}
	}
	u_close((uint64_t)fd);
	if (amount < 0) {
		u_puts("netconf: read failed\n");
		return 1;
	}
	if (!seen)
		u_puts("No PCI network controllers found.\n");
	return 0;
}
