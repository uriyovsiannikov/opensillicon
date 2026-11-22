#ifndef ABI_SYSCALL_H
#define ABI_SYSCALL_H

/* ABI v3: retired calls 4–7 and 109 are not reused. Rebuild all userspace. */
#define SYSCALL_ABI_VERSION 3U

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

#define OS_WAIT_NOHANG 1U
#define OS_SOCKET_KIND_UNIX 1U
#define OS_SOCKET_KIND_TCP 2U
#define OS_SOCKET_KIND_UDP 3U
#define POWER_CONTROL_POWEROFF 1U
#define POWER_CONTROL_REBOOT 2U

#endif
