#ifndef ABI_POLL_H
#define ABI_POLL_H
#include <stdint.h>

#define OS_POLLIN 0x001U
#define OS_POLLOUT 0x004U
#define OS_POLLERR 0x008U
#define OS_POLLHUP 0x010U
#define OS_POLLNVAL 0x020U
#define OS_POLLRDNORM 0x040U
#define OS_POLLWRNORM 0x100U
#define OS_POLL_MAX 1024U

struct os_pollfd {
	int32_t fd;
	int16_t events;
	int16_t revents;
};
#endif
