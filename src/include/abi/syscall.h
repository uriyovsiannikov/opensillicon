#ifndef ABI_SYSCALL_H
#define ABI_SYSCALL_H

/* ABI v10. Retired slots: 4–10, 14, 17, 23, 53, 58 and 109.
 * Slots are never reused; rebuild userspace after an ABI change. */
#define SYSCALL_ABI_VERSION 10U

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
#define POWER_CONTROL_POWEROFF 1U
#define POWER_CONTROL_REBOOT 2U

#endif
