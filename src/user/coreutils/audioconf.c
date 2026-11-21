#include "deviceconf.h"
int user_main(uint64_t argc, char **argv)
{
    if (argc == 2 && u_streq(argv[1], "--tone"))
        return deviceconf_show(1, argv, "audioconf", "/proc/hdatest", "[-l | --codec | --tone]");
    if (argc == 2 && u_streq(argv[1], "--codec"))
        return deviceconf_show(1, argv, "audioconf", "/proc/hdacodecs", "[-l | --codec | --tone]");
    return deviceconf_show(argc, argv, "audioconf", "/proc/audiopci", "[-l | --codec | --tone]");
}
