#include <pty.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "abi/syscall.h"
#include "syscall.h"

extern long __os_native_call(long, long, long, long, long, long, long);

int
openpty(int *master, int *slave, char *name,
    const struct termios *attributes, const struct winsize *dimensions)
{
	int pair[2], saved;
	if (master == NULL || slave == NULL) {
		errno = EINVAL;
		return -1;
	}
	/* Native PTYs are anonymous descriptors; there is no devpts pathname. */
	if (name != NULL) {
		errno = EOPNOTSUPP;
		return -1;
	}
	if (__syscall_ret(__os_native_call(SYSCALL_PTY_PAIR,
	    (long)pair, 0, 0, 0, 0, 0)) < 0)
		return -1;
	if ((attributes != NULL && tcsetattr(pair[1], TCSANOW, attributes) < 0) ||
	    (dimensions != NULL && ioctl(pair[1], TIOCSWINSZ, dimensions) < 0)) {
		saved = errno;
		close(pair[0]);
		close(pair[1]);
		errno = saved;
		return -1;
	}
	*master = pair[0];
	*slave = pair[1];
	return 0;
}
