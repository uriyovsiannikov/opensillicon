#include "deviceconf.h"

int
main(int argc, char **argv)
{
	return deviceconf_main(argc, argv, "cpuconf", "hw.cpuinfo", "[-l]", 0);
}
