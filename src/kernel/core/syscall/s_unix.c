#include "syscall_internal.h"

#include "kernel/process.h"
#include "kernel/pty.h"
#include "kernel/unix_socket.h"
#include "kernel/vmm.h"

struct interrupt_frame *
syscall_unix_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;

	switch (number) {
	case SYSCALL_UNIX_SOCKET:
		{
			int result = unix_socket_create(current_fd_table(),
			    (int)frame->rdi, (int)frame->rsi, (int)frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_UNIX_LISTEN:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result = unix_socket_listen(object, (int)frame->rsi);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UNIX_ACCEPT:
		{
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			uint32_t capacity;
			uint32_t actual = sizeof(uint16_t);
			uint16_t family = UNIX_SOCKET_AF_UNIX;
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
				    pty_is_object(object) ? fd_readiness_wait_channel() :
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
	case SYSCALL_UNIX_SHUTDOWN:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result = unix_socket_shutdown(object, (int)frame->rsi);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UNIX_GETSOCKOPT:
		{
			uint8_t value[16] = {0};
			uint32_t length;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			size_t actual;
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (frame->rsi != UNIX_SOCKET_SOL_SOCKET || frame->r8 == 0 ||
			    !vmm_copy_from_user(vmm_active_address_space(), &length,
			    frame->r8, sizeof(length)) || length > sizeof(value)) {
				syscall_error(frame, EINVAL);
				break;
			}
			actual = length;
			result = unix_socket_get_option(object, (int)frame->rdx,
			    value, &actual);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			length = (uint32_t)actual;
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->r10, actual) || !vmm_validate_user_write(
			    vmm_active_address_space(), frame->r8, sizeof(length)) ||
			    !vmm_copy_to_user(vmm_active_address_space(), frame->r10,
			    value, actual) || !vmm_copy_to_user(vmm_active_address_space(),
			    frame->r8, &length, sizeof(length))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_UNIX_SETSOCKOPT:
		{
			uint8_t value[16];
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (frame->rsi != UNIX_SOCKET_SOL_SOCKET || frame->r8 == 0 ||
			    frame->r8 > sizeof(value) || !vmm_copy_from_user(
			    vmm_active_address_space(), value, frame->r10,
			    (size_t)frame->r8)) {
				syscall_error(frame, EINVAL);
				break;
			}
			result = unix_socket_set_option(object, (int)frame->rdx,
			    value, (size_t)frame->r8);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	default:
		syscall_error(frame, ENOSYS);
		break;
	}
	return frame;
}
