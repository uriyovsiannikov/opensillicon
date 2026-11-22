#include "deviceconf.h"

int
main(int argc, char **argv)
{
	return deviceconf_main(argc, argv, "blkconf", "/proc/disks", "[-l]", 0);
}
