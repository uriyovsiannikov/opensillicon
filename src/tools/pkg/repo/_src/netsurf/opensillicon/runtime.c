#include <signal.h>
#include <stddef.h>

typedef void (*initializer)(void);
extern initializer __init_array_start[], __init_array_end[];
int __real_main(int, char **);
int __wrap_main(int, char **);

int
__wrap_main(int argc, char **argv)
{
	/* libnsfb registers each compiled surface through an ELF constructor. */
	for (initializer *entry = __init_array_start; entry < __init_array_end; entry++)
		if (*entry != NULL)
			(*entry)();
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		return 1;
	return __real_main(argc, argv);
}
