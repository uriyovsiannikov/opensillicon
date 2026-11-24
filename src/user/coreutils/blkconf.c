#include "deviceconf.h"

int
main(int argc, char **argv)
{
	return deviceconf_main(argc, argv, "blkconf", "hw.disks", "[-l]", 0);
}
