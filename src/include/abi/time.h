#ifndef ABI_TIME_H
#define ABI_TIME_H

#include <stdint.h>

#define OS_CLOCK_REALTIME 0U
#define OS_CLOCK_MONOTONIC 1U

/* Shared by clock_gettime, nanosleep requests and interrupted remainders. */
struct os_timespec {
	int64_t tv_sec;
	int64_t tv_nsec;
};

_Static_assert(sizeof(struct os_timespec) == 16, "native timespec ABI size");

#endif
