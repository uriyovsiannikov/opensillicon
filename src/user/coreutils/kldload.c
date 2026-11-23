#include <stdio.h>
#include <sys/opensillicon.h>

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fputs("usage: kldload module.ko\n", stderr);
		return 2;
	}
	if (os_module_load(argv[1]) < 0) {
		perror("kldload");
		return 1;
	}
	return 0;
}
