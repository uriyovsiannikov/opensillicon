#include "deviceconf.h"

int
main(int argc, char **argv)
{
	return deviceconf_main(argc, argv, "usbconf", "/proc/usb", "[-l]", 0);
}
