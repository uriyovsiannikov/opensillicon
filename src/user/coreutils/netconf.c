#include "deviceconf.h"

int
main(int argc, char **argv)
{
	return deviceconf_main(argc, argv, "netconf", "/proc/netpci", "[-l]",
	    "No PCI network controllers found.\n");
}
