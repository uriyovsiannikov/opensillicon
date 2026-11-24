#include "deviceconf.h"

int
main(int argc, char **argv)
{
	return deviceconf_main(argc, argv, "pciconf", "hw.pci", "[-l]", 0);
}
