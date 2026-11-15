#ifndef KERNEL_SYSCALL_INTERNAL_H
#define KERNEL_SYSCALL_INTERNAL_H

#include "arch/x86_64/interrupts.h"
#include "kernel/errno.h"
#include "kernel/process.h"
#include "kernel/syscall.h"

#define MONOTONIC_TICK_HZ 100U

#define ERRNO_EFAULT 14U
#define ERRNO_EIO 5U
#define ERRNO_ESRCH 3U
#define ERRNO_EINVAL 22U
#define ERRNO_E2BIG 7U
#define ERRNO_ENOSYS 78U
#define ERRNO_ECHILD 10U
#define ERRNO_EAGAIN 11U
#define ERRNO_ENOMEM 12U
#define ERRNO_ENAMETOOLONG 36U
#define ERRNO_EPIPE 32U
#define ERRNO_EMFILE 24U
#define ERRNO_ENOTTY 25U
#define ERRNO_ENODEV 19U

int copy_user_string(uint64_t address, char *destination, size_t capacity);
void syscall_error(struct interrupt_frame *frame, uint64_t error);

static inline struct fd_table *
current_fd_table(void)
{
	return process_current_fd_table();
}

static inline uint64_t
errno_from_kernel(int error)
{
	return error < 0 ? (uint64_t)(-error) : ERRNO_EINVAL;
}

/* Local handlers can run on APs. Returning false defers the call to the BSP
 * (for example, read/write on a descriptor that is not a pipe). */
struct interrupt_frame *syscall_device_bsp(struct interrupt_frame *frame);
struct interrupt_frame *syscall_file_bsp(struct interrupt_frame *frame);
bool syscall_io_local(struct interrupt_frame **frame);
struct interrupt_frame *syscall_io_bsp(struct interrupt_frame *frame);
bool syscall_memory_local(struct interrupt_frame **frame);
struct interrupt_frame *syscall_net_bsp(struct interrupt_frame *frame);
bool syscall_proc_local(struct interrupt_frame **frame);
struct interrupt_frame *syscall_proc_bsp(struct interrupt_frame *frame);
struct interrupt_frame *syscall_system_bsp(struct interrupt_frame *frame);
bool syscall_time_local(struct interrupt_frame **frame);
struct interrupt_frame *syscall_unix_bsp(struct interrupt_frame *frame);

#endif
