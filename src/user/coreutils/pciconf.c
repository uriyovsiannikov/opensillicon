#include "deviceconf.h"

int
main(int argc, char **argv)
{
	return deviceconf_main(argc, argv, "pciconf", "/proc/pci", "[-l]", 0);
}
