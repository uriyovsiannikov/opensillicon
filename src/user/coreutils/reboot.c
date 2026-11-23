#include <stdio.h>
#include <sys/opensillicon.h>

int
main(int argc, char **argv)
{
	(void)argv;
	if (argc != 1) {
		fputs("usage: reboot\n", stderr);
		return 2;
	}
	if (os_power_control(POWER_CONTROL_REBOOT) < 0) {
		perror("reboot");
		return 1;
	}
	return 0;
}
