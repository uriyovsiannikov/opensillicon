#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "abi/hda.h"
#include "deviceconf.h"

static int
diagnostic(unsigned request)
{
	int fd = open("/dev/hda", O_RDWR);
	if (fd < 0) {
		perror("audioconf: /dev/hda");
		return 1;
	}
	struct os_hda_report *report = malloc(sizeof(*report));
	if (report == NULL) {
		perror("audioconf");
		close(fd);
		return 1;
	}
	int result = ioctl(fd, request, report);
	if (result == 0 && report->length > sizeof(report->text)) {
		errno = EIO;
		result = -1;
	}
	if (result == 0 &&
	    fwrite(report->text, 1, report->length, stdout) != report->length)
		result = -1;
	if (result == 0 && fflush(stdout) != 0)
		result = -1;
	if (result != 0)
		perror("audioconf: HDA diagnostic");
	free(report);
	close(fd);
	return result != 0;
}
int
main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--tone"))
		return diagnostic(OS_HDA_TONE);
	if (argc == 2 && !strcmp(argv[1], "--codec"))
		return diagnostic(OS_HDA_PROBE);
	return deviceconf_main(argc, argv, "audioconf", "hw.audiopci",
	    "[-l | --codec | --tone]", NULL);
}
