#ifndef ABI_SIGNAL_H
#define ABI_SIGNAL_H

#include <stdint.h>

#define OS_SA_NODEFER 0x40000000ULL
#define OS_SA_RESETHAND 0x80000000ULL
/* Native masks use bit signo, rather than Linux's bit signo-1. */
struct os_signal_action {
	uint64_t handler;
	uint64_t restorer;
	uint64_t mask;
	uint64_t flags;
};

#define OS_SIG_BLOCK 0
#define OS_SIG_UNBLOCK 1
#define OS_SIG_SETMASK 2

#endif
