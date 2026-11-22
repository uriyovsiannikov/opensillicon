#include <string.h>

#include "deviceconf.h"

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--tone") == 0)
		return deviceconf_show("audioconf", "/proc/hdatest", 0);
	if (argc == 2 && strcmp(argv[1], "--codec") == 0)
		return deviceconf_show("audioconf", "/proc/hdacodecs", 0);
	return deviceconf_main(argc, argv, "audioconf", "/proc/audiopci",
	    "[-l | --codec | --tone]", 0);
}
