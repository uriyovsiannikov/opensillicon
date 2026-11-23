#include <stdio.h>
#include <sys/opensillicon.h>

int
main(int argc, char **argv)
{
	(void)argv;
	if (argc != 1) {
		fputs("usage: poweroff\n", stderr);
		return 2;
	}
	if (os_power_control(POWER_CONTROL_POWEROFF) < 0) {
		perror("poweroff");
		return 1;
	}
	return 0;
}
