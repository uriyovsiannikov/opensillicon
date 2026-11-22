#include <signal.h>
int __real_main(int, char **);
int __wrap_main(int, char **);

int
__wrap_main(int argc, char **argv)
{
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		return 1;
	return __real_main(argc, argv);
}
