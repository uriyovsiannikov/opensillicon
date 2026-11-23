#include "syscall_internal.h"
#include "abi/socket.h"

#include "kernel/process.h"
#include "kernel/unix_socket.h"
#include "kernel/vmm.h"

struct interrupt_frame *
syscall_unix_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;

	switch (number) {
	case SYSCALL_UNIX_ACCEPT:
		{
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			uint32_t capacity;
			uint32_t actual = sizeof(uint16_t);
			uint16_t family = OS_AF_UNIX;
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!unix_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			/* Validate all output memory before consuming a pending peer. */
			if (frame->rsi != 0 && (frame->rdx == 0 ||
			    !vmm_copy_from_user(space, &capacity, frame->rdx, sizeof(capacity)) ||
			    capacity < sizeof(family) ||
			    !vmm_validate_user_write(space, frame->rsi, sizeof(family)) ||
			    !vmm_validate_user_write(space, frame->rdx, sizeof(actual)))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = unix_socket_accept(current_fd_table(), object);
			if (result == -EAGAIN && fd_object_nonblocking(object)) {
				syscall_error(frame, EAGAIN);
				break;
			}
			if (result == -EAGAIN)
				return process_block_on_channel(frame,
				    unix_socket_wait_channel(object, true));
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else if (frame->rsi != 0) {
				if (!vmm_copy_to_user(space, frame->rsi, &family, sizeof(family)) ||
				    !vmm_copy_to_user(space, frame->rdx, &actual, sizeof(actual))) {
					(void)fd_close(current_fd_table(), (uint64_t)result);
					syscall_error(frame, EFAULT);
				} else {
					frame->rax = (uint64_t)result;
				}
			} else
				frame->rax = (uint64_t)result;
		}
		break;
	default:
		syscall_error(frame, ENOSYS);
		break;
	}
	return frame;
}
