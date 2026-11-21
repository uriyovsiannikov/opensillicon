#include <sys/select.h>
#include <errno.h>

#include "abi/select.h"
#include "abi/syscall.h"
#include "internal.h"

_Static_assert(sizeof(fd_set) == sizeof(struct os_fd_set),
	"libos fd_set ABI size mismatch");
_Static_assert(sizeof(struct timeval) == sizeof(struct os_timeval),
	"libos timeval ABI size mismatch");

int
select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	struct timeval *timeout)
{
    if(nfds<0 || nfds>FD_SETSIZE){errno=EINVAL;return -1;}
    /* Ignore unused high bits of the API's 1024-descriptor set. This also
     * interoperates with older kernels whose descriptor tables stop at 32. */
    while(nfds>0 && !(readfds && FD_ISSET(nfds-1,readfds)) &&
        !(writefds && FD_ISSET(nfds-1,writefds)) &&
        !(exceptfds && FD_ISSET(nfds-1,exceptfds)))nfds--;
	return (int)__libos_call(SYSCALL_SELECT, nfds, (long)readfds,
	    (long)writefds, (long)exceptfds, (long)timeout, 0);
}
