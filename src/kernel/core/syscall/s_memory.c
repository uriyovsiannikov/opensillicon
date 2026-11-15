#include "syscall_internal.h"

#include "kernel/vmm.h"

bool
syscall_memory_local(struct interrupt_frame **result_frame)
{
	struct interrupt_frame *frame = *result_frame;

	switch (frame->rax) {
	case SYSCALL_MMAP:
		{
			uint64_t base;
			int result;

			if (frame->rsi > UINT32_MAX)
				result = -EINVAL;
			else
				result = vmm_private_map_protected(
				    vmm_active_address_space(), frame->rdi,
				    (unsigned int)frame->rsi, &base);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = base;
		}
		return true;
	case SYSCALL_MUNMAP:
		{
			int result = vmm_private_unmap_complete(
			    vmm_active_address_space(), frame->rdi, frame->rsi);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		return true;
	case SYSCALL_MPROTECT:
		{
			int result;

			if (frame->rdx > UINT32_MAX) {
				result = -EINVAL;
			} else {
				result = vmm_private_protect(
				    vmm_active_address_space(), frame->rdi,
				    frame->rsi, (unsigned int)frame->rdx);
			}
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		return true;
	case SYSCALL_SHM_CREATE:
		{
			uint64_t base;
			int result = vmm_shm_create(vmm_active_address_space(),
			    frame->rdi, &base);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = base;
		}
		return true;
	case SYSCALL_SHM_DETACH:
		{
			int result = vmm_shm_detach(vmm_active_address_space(),
			    frame->rdi);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		return true;
	default:
		return false;
	}
}
