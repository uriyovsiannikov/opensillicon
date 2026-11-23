#ifndef ABI_SYSCALL_H
#define ABI_SYSCALL_H

/* ABI v21. Retired slots: 4–10, 14, 17, 23, 33, 34, 41, 43–46, 53, 58,
 * 63, 81–84, 88, 89, 91–94, 101 and 109.
 * Slots are never reused; rebuild userspace after an ABI change. */
#define SYSCALL_ABI_VERSION 21U

#ifdef __ASSEMBLER__
#define OS_SYSCALL(name, number) .equ name, number ;
#include "abi/syscall_list.h"
#undef OS_SYSCALL
#else
enum os_syscall_number {
#define OS_SYSCALL(name, number) name = number,
#include "abi/syscall_list.h"
#undef OS_SYSCALL
};
#endif

#define OS_AT_FDCWD (-100)
#define OS_WAIT_NOHANG 1U
#define OS_SOCKET_KIND_UNIX 1U
#define OS_SOCKET_KIND_TCP 2U
#define OS_SOCKET_KIND_UDP 3U
#include "abi/power.h"

#endif
