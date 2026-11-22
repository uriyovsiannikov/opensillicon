#include <stdio.h>
#include <sys/utsname.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	enum { SYSTEM = 1, NODE = 2, RELEASE = 4, VERSION = 8, MACHINE = 16, ALL = 31 };
	unsigned selected = 0;
	int option;
	while ((option = getopt(argc, argv, "asnrvm")) != -1) {
		switch (option) {
		case 'a': selected = ALL; break;
		case 's': selected |= SYSTEM; break;
		case 'n': selected |= NODE; break;
		case 'r': selected |= RELEASE; break;
		case 'v': selected |= VERSION; break;
		case 'm': selected |= MACHINE; break;
		default: fputs("usage: uname [-asnrvm]\n", stderr); return 2;
		}
	}
	if (optind != argc) {
		fputs("usage: uname [-asnrvm]\n", stderr);
		return 2;
	}
	if (selected == 0)
		selected = SYSTEM;
	struct utsname identity;
	if (uname(&identity) < 0) {
		perror("uname");
		return 1;
	}
	const char *fields[] = {identity.sysname, identity.nodename, identity.release,
	    identity.version, identity.machine};
	int emitted = 0;
	for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		if ((selected & (1U << i)) == 0)
			continue;
		if ((emitted && putchar(' ') == EOF) || fputs(fields[i], stdout) == EOF)
			return 1;
		emitted = 1;
	}
	return putchar('\n') == EOF;
}
