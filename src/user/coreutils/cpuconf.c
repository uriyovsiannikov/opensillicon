#include "deviceconf.h"

int
main(int argc, char **argv)
{
	return deviceconf_main(argc, argv, "cpuconf", "/proc/cpuinfo", "[-l]", 0);
}
