#include "syscall_internal.h"

#include "abi/select.h"
#include "kernel/pipe.h"
#include "kernel/process.h"
#include "kernel/pty.h"
#include "kernel/smp.h"
#include "kernel/tcp_socket.h"
#include "kernel/timer.h"
#include "kernel/tty.h"
#include "kernel/unix_socket.h"
#include "kernel/vfs.h"
#include "kernel/vmm.h"

#define SYSCALL_MAX_IO 4096U
#define SYSCALL_MAX_IOV 16U
#define SYSCALL_MAX_IOV_BYTES (64U * 1024U)
#define SYSCALL_IOV_CHUNK 512U
#define NATIVE_F_GETFD 1U
#define NATIVE_F_SETFD 2U
#define NATIVE_F_GETFL 3U
#define NATIVE_F_SETFL 4U
#define NATIVE_F_SETOWN 8U
#define NATIVE_O_NONBLOCK FD_STATUS_NONBLOCK
#define NATIVE_O_ACCMODE 3U

struct syscall_iovec {
	uint64_t base;
	uint64_t length;
};

static bool
fd_set_contains(const struct os_fd_set *set, unsigned int descriptor)
{
	return set != NULL && (set->words[descriptor / 64U] &
	    (1ULL << (descriptor % 64U))) != 0;
}

static void
fd_set_add(struct os_fd_set *set, unsigned int descriptor)
{
	set->words[descriptor / 64U] |= 1ULL << (descriptor % 64U);
}

static struct interrupt_frame *
syscall_select(struct interrupt_frame *frame)
{
	struct os_fd_set requested_read = {0}, requested_write = {0};
	struct os_fd_set requested_except = {0};
	struct os_fd_set ready_read = {0}, ready_write = {0};
	struct os_fd_set ready_except = {0};
	struct os_timeval timeout;
	uint64_t nfds = frame->rdi;
	uint64_t deadline = process_select_deadline();
	uint64_t now;
	uint64_t wait_ticks = 0;
	int ready_count = 0;
	bool has_timeout = frame->r8 != 0;
	bool poll_terminal = false;
	if (nfds > FD_TABLE_CAPACITY) {
		process_select_deadline_clear();
		syscall_error(frame, EINVAL);
		return frame;
	}
	if ((frame->rsi != 0 && !vmm_copy_from_user(vmm_active_address_space(),
	    &requested_read, frame->rsi, sizeof(requested_read))) ||
	    (frame->rdx != 0 && !vmm_copy_from_user(vmm_active_address_space(),
	    &requested_write, frame->rdx, sizeof(requested_write))) ||
	    (frame->r10 != 0 && !vmm_copy_from_user(vmm_active_address_space(),
	    &requested_except, frame->r10, sizeof(requested_except)))) {
		process_select_deadline_clear();
		syscall_error(frame, EFAULT);
		return frame;
	}
	if (has_timeout && !vmm_copy_from_user(vmm_active_address_space(),
	    &timeout, frame->r8, sizeof(timeout))) {
		process_select_deadline_clear();
		syscall_error(frame, EFAULT);
		return frame;
	}
	if (has_timeout && (timeout.seconds < 0 || timeout.microseconds < 0 ||
	    timeout.microseconds >= 1000000)) {
		process_select_deadline_clear();
		syscall_error(frame, EINVAL);
		return frame;
	}
	if ((frame->rsi != 0 && !vmm_validate_user_write(
	    vmm_active_address_space(), frame->rsi, sizeof(ready_read))) ||
	    (frame->rdx != 0 && !vmm_validate_user_write(
	    vmm_active_address_space(), frame->rdx, sizeof(ready_write))) ||
	    (frame->r10 != 0 && !vmm_validate_user_write(
	    vmm_active_address_space(), frame->r10, sizeof(ready_except)))) {
		process_select_deadline_clear();
		syscall_error(frame, EFAULT);
		return frame;
	}
	for (unsigned int descriptor = 0; descriptor < nfds; descriptor++) {
		uint32_t requested = 0, ready;
		struct fd_object *object;
		if (fd_set_contains(&requested_read, descriptor))
			requested |= FD_POLL_READ;
		if (fd_set_contains(&requested_write, descriptor))
			requested |= FD_POLL_WRITE;
		if (fd_set_contains(&requested_except, descriptor))
			requested |= FD_POLL_ERROR;
		if (requested == 0)
			continue;
		object = fd_object_for_descriptor(current_fd_table(), descriptor);
		if (object == NULL) {
			process_select_deadline_clear();
			syscall_error(frame, EBADF);
			return frame;
		}
		if ((requested & FD_POLL_READ) != 0 && fd_object_is_console(object)) {
			poll_terminal = true;
			ready = tty_input_ready() ? FD_POLL_READ : 0;
			ready |= fd_object_poll(object, requested & ~FD_POLL_READ);
		} else {
			ready = fd_object_poll(object, requested);
		}
		if (ready != 0)
			ready_count++;
		if ((ready & FD_POLL_READ) != 0) {
			fd_set_add(&ready_read, descriptor);
		}
		if ((ready & FD_POLL_WRITE) != 0) {
			fd_set_add(&ready_write, descriptor);
		}
		if ((ready & FD_POLL_ERROR) != 0) {
			fd_set_add(&ready_except, descriptor);
		}
	}
	if (ready_count != 0) {
		process_select_deadline_clear();
		if ((frame->rsi != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->rsi, &ready_read, sizeof(ready_read))) ||
		    (frame->rdx != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->rdx, &ready_write, sizeof(ready_write))) ||
		    (frame->r10 != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->r10, &ready_except, sizeof(ready_except)))) {
			syscall_error(frame, EFAULT);
			return frame;
		}
		frame->rax = (uint64_t)ready_count;
		return frame;
	}
	if (deadline == 0) {
		if (!has_timeout) {
			deadline = UINT64_MAX;
		} else {
			if ((uint64_t)timeout.seconds >
			    (UINT64_MAX - MONOTONIC_TICK_HZ) / MONOTONIC_TICK_HZ) {
				process_select_deadline_clear();
				syscall_error(frame, EINVAL);
				return frame;
			}
			wait_ticks = (uint64_t)timeout.seconds *
			    MONOTONIC_TICK_HZ;
			wait_ticks += ((uint64_t)timeout.microseconds *
			    MONOTONIC_TICK_HZ + 999999U) / 1000000U;
			now = timer_ticks();
			if (wait_ticks > UINT64_MAX - now) {
				process_select_deadline_clear();
				syscall_error(frame, EINVAL);
				return frame;
			}
			deadline = now + wait_ticks;
		}
		process_select_deadline_set(deadline);
	}
	if (deadline != UINT64_MAX && timer_ticks() >= deadline) {
		process_select_deadline_clear();
		if ((frame->rsi != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->rsi, &ready_read, sizeof(ready_read))) ||
		    (frame->rdx != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->rdx, &ready_write, sizeof(ready_write))) ||
		    (frame->r10 != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->r10, &ready_except, sizeof(ready_except)))) {
			syscall_error(frame, EFAULT);
			return frame;
		}
		frame->rax = 0;
		return frame;
	}
	/* The PS/2 console reads hardware events by polling. Retry on the
	 * next timer tick even when select has no user timeout. */
	if (poll_terminal) {
		uint64_t next_tick = timer_ticks() + 1;
		if (next_tick < deadline)
			deadline = next_tick;
	}
	return process_block_on_channel_until(frame, fd_readiness_wait_channel(),
	    deadline == UINT64_MAX ? 0 : deadline);
}

static int
load_iovecs(struct interrupt_frame *frame, struct syscall_iovec *vectors,
	uint64_t *total)
{
	struct address_space *space = vmm_active_address_space();
	uint64_t count = frame->rdx;
	uint64_t total_bytes = 0;
	uint64_t i;

	if (count > SYSCALL_MAX_IOV)
		return -(int)ERRNO_EINVAL;
	if (count != 0 && (frame->rsi > UINT64_MAX -
	    count * sizeof(*vectors) || !vmm_copy_from_user(space, vectors,
	    frame->rsi, (size_t)(count * sizeof(*vectors)))))
		return -(int)ERRNO_EFAULT;
	for (i = 0; i < count; i++) {
		if (vectors[i].length > SYSCALL_MAX_IOV_BYTES - total_bytes)
			return -(int)ERRNO_EINVAL;
		total_bytes += vectors[i].length;
		if (vectors[i].length != 0 &&
		    vectors[i].base > UINT64_MAX - vectors[i].length)
			return -(int)ERRNO_EFAULT;
	}
	*total = total_bytes;
	return 0;
}

static int
validate_iovec_buffers(const struct syscall_iovec *vectors, uint64_t count,
	bool for_read)
{
	struct address_space *space = vmm_active_address_space();
	char scratch[SYSCALL_IOV_CHUNK];
	uint64_t i;

	for (i = 0; i < count; i++) {
		uint64_t offset = 0;

		if (vectors[i].length == 0)
			continue;
		if (for_read) {
			if (!vmm_validate_user_write(space, vectors[i].base,
			    (size_t)vectors[i].length))
				return -(int)ERRNO_EFAULT;
			continue;
		}
		while (offset < vectors[i].length) {
			size_t amount = (size_t)(vectors[i].length - offset);

			if (amount > sizeof(scratch))
				amount = sizeof(scratch);
			if (!vmm_copy_from_user(space, scratch,
			    vectors[i].base + offset, amount))
				return -(int)ERRNO_EFAULT;
			offset += amount;
		}
	}
	return 0;
}

/* Keep a complete small writev indivisible, just like one scalar pipe write. */
static struct interrupt_frame *
syscall_pipe_writev(struct interrupt_frame *frame, struct fd_object *object,
	const struct syscall_iovec *vectors, uint64_t total)
{
	char buffer[PIPE_CAPACITY];
	size_t offset = 0, amount = 0;
	for (uint64_t i = 0; i < frame->rdx; i++) {
		if (vectors[i].length != 0 && !vmm_copy_from_user(vmm_active_address_space(),
		    buffer + offset, vectors[i].base, (size_t)vectors[i].length)) {
			syscall_error(frame, EFAULT);
			return frame;
		}
		offset += (size_t)vectors[i].length;
	}
	int result = pipe_write_try(object, buffer, (size_t)total, &amount);
	if (result == -EAGAIN && !fd_object_nonblocking(object))
		return process_block_local_on_channel(frame, pipe_wait_channel(object, PIPE_WRITE_WAIT));
	if (result != 0)
		syscall_error(frame, errno_from_kernel(result));
	else
		frame->rax = amount;
	return frame;
}

static struct interrupt_frame *
syscall_writev(struct interrupt_frame *frame)
{
	struct syscall_iovec vectors[SYSCALL_MAX_IOV];
	struct os_credentials credentials;
	char buffer[SYSCALL_IOV_CHUNK];
	struct fd_table *table = current_fd_table();
	struct fd_object *object = fd_object_for_descriptor(table, frame->rdi);
	uint64_t count = frame->rdx;
	uint64_t total = 0;
	uint64_t written = 0;
	uint64_t i;
	int result;

	if (object == NULL || object->operations == NULL ||
	    object->operations->write == NULL) {
		syscall_error(frame, 9);
		return frame;
	}
	if (!process_current_credentials(&credentials)) {
		syscall_error(frame, ERRNO_ESRCH);
		return frame;
	}
	result = load_iovecs(frame, vectors, &total);
	if (result == 0)
		result = validate_iovec_buffers(vectors, count, false);
	if (result != 0) {
		syscall_error(frame, errno_from_kernel(result));
		return frame;
	}
	if (pipe_is_write_endpoint(object) && total <= PIPE_CAPACITY)
		return syscall_pipe_writev(frame, object, vectors, total);
	for (i = 0; i < count; i++) {
		uint64_t offset = 0;

		while (offset < vectors[i].length) {
			size_t chunk = (size_t)(vectors[i].length - offset);
			size_t amount = 0;

			if (chunk > sizeof(buffer))
				chunk = sizeof(buffer);
			if (!vmm_copy_from_user(vmm_active_address_space(), buffer,
			    vectors[i].base + offset, chunk)) {
				if (written != 0)
					goto partial;
				syscall_error(frame, ERRNO_EFAULT);
				return frame;
			}
			if (pipe_is_write_endpoint(object)) {
				result = pipe_write_try(object, buffer, chunk, &amount);
				if (result == -(int)ERRNO_EAGAIN) {
					if (written != 0)
						goto partial;
					if (fd_object_nonblocking(object)) {
						syscall_error(frame, ERRNO_EAGAIN);
						return frame;
					}
					return process_block_local_on_channel(frame,
					    pipe_wait_channel(object, PIPE_WRITE_WAIT));
				}
			} else {
				result = fd_write_as(table, frame->rdi, buffer, chunk,
				    &amount, credentials.effective_uid);
				if (result == -(int)ERRNO_EAGAIN &&
				    (unix_socket_is_object(object) || pty_is_object(object) ||
				    tcp_socket_is_object(object))) {
					if (written != 0)
						goto partial;
					if (fd_object_nonblocking(object)) {
						syscall_error(frame, ERRNO_EAGAIN);
						return frame;
					}
					return process_block_on_channel(frame,
					    unix_socket_is_object(object) ?
					    unix_socket_wait_channel(object, false) :
					    fd_readiness_wait_channel());
				}
			}
			if (result != 0) {
				if (written != 0)
					goto partial;
				syscall_error(frame, errno_from_kernel(result));
				return frame;
			}
			written += amount;
			offset += amount;
			if (amount < chunk)
				goto partial;
		}
	}
partial:
	(void)total;
	frame->rax = written;
	return frame;
}

static bool
pipe_copy_user(void *context, const void *source, size_t length)
{
	return vmm_copy_to_user(vmm_active_address_space(), (uintptr_t)context,
	    source, length);
}

static struct interrupt_frame *
syscall_readv(struct interrupt_frame *frame)
{
	struct syscall_iovec vectors[SYSCALL_MAX_IOV];
	char buffer[SYSCALL_IOV_CHUNK];
	struct fd_table *table = current_fd_table();
	struct fd_object *object = fd_object_for_descriptor(table, frame->rdi);
	uint64_t count = frame->rdx;
	uint64_t total = 0;
	uint64_t received = 0;
	uint64_t i;
	int result;

	if (object == NULL || object->operations == NULL ||
	    object->operations->read == NULL) {
		syscall_error(frame, 9);
		return frame;
	}
	result = load_iovecs(frame, vectors, &total);
	if (result == 0)
		result = validate_iovec_buffers(vectors, count, true);
	if (result != 0) {
		syscall_error(frame, errno_from_kernel(result));
		return frame;
	}
	for (i = 0; i < count; i++) {
		uint64_t offset = 0;

		while (offset < vectors[i].length) {
			size_t chunk = (size_t)(vectors[i].length - offset);
			size_t amount = 0;
			uint64_t old_offset = pipe_is_read_endpoint(object) ? 0 :
			    fd_get_offset(table, frame->rdi);

			if (chunk > sizeof(buffer))
				chunk = sizeof(buffer);
			/* TTY reads may sleep until more input arrives. Once one
			 * fragment has been delivered, return it rather than sleeping
			 * for a later iovec. */
			if (received != 0 && fd_object_is_terminal(object))
				goto partial;
			if (pipe_is_read_endpoint(object)) {
				result = pipe_read_to(object, chunk, pipe_copy_user,
				    (void *)(uintptr_t)(vectors[i].base + offset), &amount);
				if (result == -(int)ERRNO_EAGAIN) {
					if (received != 0)
						goto partial;
					if (fd_object_nonblocking(object)) {
						syscall_error(frame, ERRNO_EAGAIN);
						return frame;
					}
					return process_block_local_on_channel(frame,
					    pipe_wait_channel(object, PIPE_READ_WAIT));
				}
			} else {
				result = fd_read(table, frame->rdi, buffer, chunk,
				    &amount);
				if (result == -(int)ERRNO_EAGAIN &&
				    (unix_socket_is_object(object) || pty_is_object(object) ||
				    tcp_socket_is_object(object))) {
					if (received != 0)
						goto partial;
					if (fd_object_nonblocking(object)) {
						syscall_error(frame, ERRNO_EAGAIN);
						return frame;
					}
					return process_block_on_channel(frame,
					    unix_socket_is_object(object) ?
					    unix_socket_wait_channel(object, true) :
					    fd_readiness_wait_channel());
				}
			}
			if (result != 0) {
				if (received != 0)
					goto partial;
				syscall_error(frame, errno_from_kernel(result));
				return frame;
			}
			if (!pipe_is_read_endpoint(object) && amount != 0 && !vmm_copy_to_user(
			    vmm_active_address_space(), vectors[i].base + offset,
			    buffer, amount)) {
				if (!pipe_is_read_endpoint(object))
					(void)fd_set_offset(table, frame->rdi, old_offset);
				if (received != 0)
					goto partial;
				syscall_error(frame, ERRNO_EFAULT);
				return frame;
			}
			received += amount;
			offset += amount;
			if (amount < chunk)
				goto partial;
		}
	}
partial:
	(void)total;
	frame->rax = received;
	return frame;
}

/* Only pipe endpoints enter this path. Local waits keep the original frame
 * for a kernel continuation, rather than rewinding a user int instruction. */
static bool
syscall_pipe_io(struct interrupt_frame **result_frame)
{
	struct interrupt_frame *frame = *result_frame;
	struct fd_object *object = fd_object_for_descriptor(current_fd_table(), frame->rdi);
	bool reading = frame->rax == SYSCALL_READ;
	if (reading ? !pipe_is_read_endpoint(object) : !pipe_is_write_endpoint(object))
		return false;
	if (frame->rdx > SYSCALL_MAX_IO) {
		syscall_error(frame, EINVAL);
		return true;
	}
	size_t amount = 0;
	int result;
	if (reading) {
		result = pipe_read_to(object, (size_t)frame->rdx, pipe_copy_user,
		    (void *)(uintptr_t)frame->rsi, &amount);
	} else {
		char buffer[SYSCALL_MAX_IO];
		if (frame->rdx != 0 && !vmm_copy_from_user(vmm_active_address_space(),
		    buffer, frame->rsi, (size_t)frame->rdx)) {
			syscall_error(frame, EFAULT);
			return true;
		}
		result = pipe_write_try(object, buffer, (size_t)frame->rdx, &amount);
	}
	if (result == -EAGAIN && !fd_object_nonblocking(object)) {
		*result_frame = process_block_local_on_channel(frame,
		    pipe_wait_channel(object, reading ? PIPE_READ_WAIT : PIPE_WRITE_WAIT));
		return true;
	}
	if (result != 0) {
		syscall_error(frame, errno_from_kernel(result));
	} else {
		frame->rax = amount;
		struct smp_cpu *cpu = smp_current_cpu();
		if (amount != 0 && cpu != NULL && !cpu->bsp)
			__atomic_store_n(&cpu->user_pipe_io, true, __ATOMIC_RELAXED);
	}
	return true;
}

bool
syscall_io_local(struct interrupt_frame **result_frame)
{
	struct interrupt_frame *frame = *result_frame;

	switch (frame->rax) {
	case SYSCALL_READV:
	case SYSCALL_WRITEV:
		{
			struct fd_object *object = fd_object_for_descriptor(current_fd_table(), frame->rdi);
			bool reading = frame->rax == SYSCALL_READV;
			if (reading ? !pipe_is_read_endpoint(object) : !pipe_is_write_endpoint(object))
				return false;
			*result_frame = reading ? syscall_readv(frame) : syscall_writev(frame);
			struct smp_cpu *cpu = smp_current_cpu();
			if (cpu != NULL && !cpu->bsp && ((*result_frame)->rflags & 1U) == 0 &&
			    (*result_frame)->rax != 0)
				__atomic_store_n(&cpu->user_pipe_vectors, true, __ATOMIC_RELAXED);
		}
		return true;
	case SYSCALL_READ:
	case SYSCALL_WRITE:
		return syscall_pipe_io(result_frame);
	case SYSCALL_CLOSE:
		{
			struct fd_table *table = current_fd_table();
			struct fd_object *object = fd_object_for_descriptor(table, frame->rdi);
			if (!pipe_is_read_endpoint(object) && !pipe_is_write_endpoint(object))
				return false;
			int result = fd_close(table, frame->rdi);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		return true;
	case SYSCALL_PIPE:
		{
			uint32_t descriptors[2];
			int reader, writer;
			struct address_space *space = vmm_active_address_space();
			struct fd_table *table = current_fd_table();
			if (!vmm_validate_user_write(space, frame->rdi, sizeof(descriptors))) {
				syscall_error(frame, EFAULT);
				return true;
			}
			int result = pipe_create(table, &reader, &writer);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				return true;
			}
			descriptors[0] = (uint32_t)reader;
			descriptors[1] = (uint32_t)writer;
			if (!vmm_copy_to_user(space, frame->rdi, descriptors, sizeof(descriptors))) {
				(void)fd_close(table, reader);
				(void)fd_close(table, writer);
				syscall_error(frame, EFAULT);
			} else {
				frame->rax = 0;
			}
		}
		return true;
	default:
		return false;
	}
}

struct interrupt_frame *
syscall_io_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;
	struct fd_table *table;
	size_t amount;

	switch (number) {
	case SYSCALL_WRITE:
		if (frame->rdx > SYSCALL_MAX_IO) {
			syscall_error(frame, ERRNO_EINVAL);
			break;
		}
		{
			char buffer[SYSCALL_MAX_IO];
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object;
			struct os_credentials credentials;
			int result;

			if (frame->rdx != 0 &&
			    !vmm_copy_from_user(space, buffer, frame->rsi,
			    (size_t)frame->rdx)) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			table = current_fd_table();
			object = fd_object_for_descriptor(table, frame->rdi);
			if (!process_current_credentials(&credentials)) {
				syscall_error(frame, ERRNO_ESRCH);
				break;
			}
			if (pipe_is_write_endpoint(object)) {
				result = pipe_write_try(object, buffer,
				    (size_t)frame->rdx, &amount);
				if (result == -(int)ERRNO_EAGAIN &&
				    fd_object_nonblocking(object)) {
					syscall_error(frame, ERRNO_EAGAIN);
					break;
				}
				if (result == -(int)ERRNO_EAGAIN)
					return process_block_local_on_channel(frame,
					    pipe_wait_channel(object, PIPE_WRITE_WAIT));
				if (result != 0) {
					syscall_error(frame, errno_from_kernel(result));
					break;
				}
				frame->rax = amount;
				break;
			}
			result = fd_write_as(table, frame->rdi, buffer,
			    (size_t)frame->rdx, &amount, credentials.effective_uid);
			if (result == -(int)ERRNO_EAGAIN &&
			    (unix_socket_is_object(object) || pty_is_object(object) ||
			    tcp_socket_is_object(object)) &&
			    fd_object_nonblocking(object)) {
				syscall_error(frame, ERRNO_EAGAIN);
				break;
			}
			if (result == -(int)ERRNO_EAGAIN &&
			    (unix_socket_is_object(object) || pty_is_object(object) ||
			    tcp_socket_is_object(object)))
				return process_block_on_channel(frame,
				    unix_socket_is_object(object) ?
				    unix_socket_wait_channel(object, false) :
				    fd_readiness_wait_channel());
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			frame->rax = amount;
		}
		break;
	case SYSCALL_WRITEV:
		return syscall_writev(frame);
	case SYSCALL_READV:
		return syscall_readv(frame);
	case SYSCALL_CLOSE:
		{
			int result = fd_close(current_fd_table(), frame->rdi);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_FCNTL:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			switch (frame->rsi) {
			case NATIVE_F_GETFD: {
				uint32_t flags;
				int result = fd_get_descriptor_flags(current_fd_table(),
				    frame->rdi, &flags);
				if (result != 0)
					syscall_error(frame, errno_from_kernel(result));
				else
					frame->rax = flags;
				break;
			}
			case NATIVE_F_SETFD: {
				int result = frame->rdx > UINT32_MAX ? -EINVAL :
				    fd_set_descriptor_flags(current_fd_table(),
				    frame->rdi, (uint32_t)frame->rdx);
				if (result != 0)
					syscall_error(frame, errno_from_kernel(result));
				else
					frame->rax = 0;
				break;
			}
			case NATIVE_F_GETFL:
				frame->rax = __atomic_load_n(&object->status_flags, __ATOMIC_RELAXED);
				break;
			case NATIVE_F_SETFL:
				__atomic_store_n(&object->status_flags,
				    (__atomic_load_n(&object->status_flags, __ATOMIC_RELAXED) &
				    NATIVE_O_ACCMODE) | ((uint32_t)frame->rdx &
				    NATIVE_O_NONBLOCK), __ATOMIC_RELAXED);
				fd_readiness_wake();
				frame->rax = 0;
				break;
			case NATIVE_F_SETOWN:
				syscall_error(frame, EOPNOTSUPP);
				break;
			default:
				syscall_error(frame, EOPNOTSUPP);
				break;
			}
		}
		break;
	case SYSCALL_DUP2:
		{
			int result;
			if (frame->rdi > UINT32_MAX || frame->rsi > UINT32_MAX) {
				syscall_error(frame, EBADF);
				break;
			}
			result = fd_dup2(current_fd_table(), frame->rdi, frame->rsi);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_DUP:
		{
			int result;
			if (frame->rdi > UINT32_MAX) {
				syscall_error(frame, EBADF);
				break;
			}
			result = fd_dup(current_fd_table(), frame->rdi);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_LSEEK:
		{
			uint64_t position;
			int result;

			if (frame->rdx > 2U) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = fd_seek(current_fd_table(), frame->rdi,
			    (int64_t)frame->rsi, (int)frame->rdx, &position);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = position;
		}
		break;
	case SYSCALL_READ:
		if (frame->rdx > SYSCALL_MAX_IO) {
			syscall_error(frame, ERRNO_EINVAL);
			break;
		}
		{
			char buffer[SYSCALL_MAX_IO];
			uint64_t old_offset = fd_get_offset(current_fd_table(), frame->rdi);
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;

			/* Validate before a read can consume socket, terminal, or device
			 * data. Restoring a file offset cannot undo those side effects. */
			if (object != NULL &&
			    !vmm_validate_user_write(vmm_active_address_space(),
			    frame->rsi, (size_t)frame->rdx)) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}

			if (pipe_is_read_endpoint(object)) {
				result = pipe_read_to(object, (size_t)frame->rdx,
				    pipe_copy_user, (void *)(uintptr_t)frame->rsi, &amount);
				if (result == -(int)ERRNO_EAGAIN &&
				    fd_object_nonblocking(object)) {
					syscall_error(frame, ERRNO_EAGAIN);
					break;
				}
				if (result == -(int)ERRNO_EAGAIN)
					return process_block_local_on_channel(frame,
					    pipe_wait_channel(object, PIPE_READ_WAIT));
				if (result != 0) {
					syscall_error(frame, errno_from_kernel(result));
					break;
				}
				frame->rax = amount;
				break;
			}
			result = fd_read(current_fd_table(), frame->rdi, buffer,
			    (size_t)frame->rdx, &amount);
			if (result == -(int)ERRNO_EAGAIN &&
			    (unix_socket_is_object(object) || pty_is_object(object) ||
			    tcp_socket_is_object(object)) &&
			    fd_object_nonblocking(object)) {
				syscall_error(frame, ERRNO_EAGAIN);
				break;
			}
			if (result == -(int)ERRNO_EAGAIN &&
			    (unix_socket_is_object(object) || pty_is_object(object) ||
			    tcp_socket_is_object(object)))
				return process_block_on_channel(frame,
				    unix_socket_is_object(object) ?
				    unix_socket_wait_channel(object, true) :
				    fd_readiness_wait_channel());

			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (amount != 0 && !vmm_copy_to_user(
			    vmm_active_address_space(), frame->rsi, buffer, amount)) {
				(void)fd_set_offset(current_fd_table(), frame->rdi, old_offset);
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = amount;
		}
		break;
	case SYSCALL_SELECT:
		return syscall_select(frame);
	default:
		syscall_error(frame, ERRNO_ENOSYS);
		break;
	}
	return frame;
}
