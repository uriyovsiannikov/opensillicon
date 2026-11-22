#include <sys/icmp.h>
#include "abi/syscall.h"
#include "syscall.h"

extern long __os_native_call(long, long, long, long, long, long, long);

int
os_icmp_echo(struct os_icmp_echo_request *request)
{
	return __syscall_ret(__os_native_call(SYSCALL_ICMP_ECHO, (long)request,
	    0, sizeof(*request), 0, 0, 0));
}
