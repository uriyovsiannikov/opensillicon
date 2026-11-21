#include <stdarg.h>
#include <stddef.h>
#include <sys/ioctl.h>

#include "abi/syscall.h"
#include "abi/disk.h"
#include "abi/audio.h"
#include "internal.h"

int
ioctl(int fd, unsigned long request, ...)
{
	va_list args;
	void *argument;
	va_start(args, request);
	argument = (request == OS_DIOCGFLUSH || request == OS_DIOCREREADGPT ||
	    request == OS_AUDIO_DRAIN || request == OS_AUDIO_RESET)
	    ? NULL
	    : va_arg(args, void *);
	va_end(args);
	return (int)__libos_call(SYSCALL_IOCTL, fd, (long)request,
	    (long)argument, 0, 0, 0);
}
