#include "deviceconf.h"
int user_main(uint64_t argc, char **argv)
{ return deviceconf_show(argc, argv, "pciconf", "/proc/pci", "[-l]"); }
