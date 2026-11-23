#include <stdio.h>
#include <sys/opensillicon.h>

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fputs("usage: kldunload module-name\n", stderr);
		return 2;
	}
	if (os_module_unload(argv[1]) < 0) {
		perror("kldunload");
		return 1;
	}
	return 0;
}
