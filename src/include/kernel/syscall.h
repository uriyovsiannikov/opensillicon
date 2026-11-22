#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "abi/syscall.h"
#include "kernel/fd.h"

/* Duplicate case labels reject duplicate assignments at compile time. */
static inline void
syscall_validate_numbers(void)
{
	switch (0) {
#define OS_SYSCALL(name, number) case number: break;
#include "abi/syscall_list.h"
#undef OS_SYSCALL
	}
}

struct interrupt_frame;
struct interrupt_frame *syscall_dispatch(struct interrupt_frame *frame);
void syscall_init(void);

#endif
