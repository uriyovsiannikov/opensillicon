#include "../../user/coreutils/deviceconf.h"

int
main(int argc, char **argv)
{
	if (argc < 2)
		return 2;
	return deviceconf_main(argc - 1, argv + 1, "device-test", argv[1],
	    "[-l]", "No devices found.\n");
}
