#include "syscall_internal.h"

#include "kernel/process.h"
#include "kernel/heap.h"
#include "kernel/vfs.h"
#include "kernel/vmm.h"

/* Copy a terminated argv/envp vector once, without retaining user pointers. */
static uint64_t
copy_exec_vector(uint64_t vector, char *storage, size_t capacity,
	const char **items, size_t limit, size_t *count)
{
	size_t used = 0;

	*count = 0;
	while (vector != 0) {
		uint64_t address;
		if (vector > UINT64_MAX - *count * sizeof(address) ||
		    !vmm_copy_from_user(vmm_active_address_space(), &address,
		    vector + *count * sizeof(address), sizeof(address)))
			return ERRNO_EFAULT;
		if (address == 0)
			return 0;
		if (*count == limit || used == capacity)
			return ERRNO_E2BIG;
		int rc = copy_user_string(address, storage + used, capacity - used);
		if (rc != 0)
			return rc == -(int)ERRNO_ENAMETOOLONG ? ERRNO_E2BIG : (uint64_t)-rc;
		items[(*count)++] = storage + used;
		while (storage[used++] != '\0')
			;
	}
	return 0;
}

bool
syscall_proc_local(struct interrupt_frame **result_frame)
{
	struct interrupt_frame *frame = *result_frame;
	uint64_t error;

	switch (frame->rax) {
	case SYSCALL_GET_ABI_VERSION:
		frame->rax = SYSCALL_ABI_VERSION;
		return true;
	case SYSCALL_GETPID:
		frame->rax = process_current_pid();
		return true;
	case SYSCALL_GETCREDENTIALS:
		{
			struct os_credentials credentials;
			struct address_space *space = vmm_active_address_space();
			if (!vmm_validate_user_write(space, frame->rdi,
			    sizeof(credentials)) ||
			    !process_current_credentials(&credentials) ||
			    !vmm_copy_to_user(space, frame->rdi, &credentials,
			    sizeof(credentials)))
				syscall_error(frame, ERRNO_EFAULT);
			else
				frame->rax = 0;
		}
		return true;
	case SYSCALL_SET_FS_BASE:
		error = process_set_fs_base(frame->rdi);
		if (error != 0)
			syscall_error(frame, error);
		else
			frame->rax = 0;
		return true;
	case SYSCALL_SIGACTION:
		if (frame->rdi > UINT32_MAX)
			syscall_error(frame, ERRNO_EINVAL);
		else {
			(void)process_sigaction(frame, (unsigned int)frame->rdi,
			    frame->rsi, frame->rdx, &error);
			if (error != 0)
				syscall_error(frame, error);
		}
		return true;
	case SYSCALL_SIGRETURN:
		*result_frame = process_sigreturn(frame, &error);
		if (error != 0)
			syscall_error(*result_frame, error);
		return true;
	case SYSCALL_SIGPROCMASK:
		{
			uint64_t requested, previous;
			uint64_t *requested_pointer = NULL;
			uint64_t *previous_pointer = NULL;
			int result;
			struct address_space *space = vmm_active_address_space();

			if (frame->rsi != 0) {
				if (!vmm_copy_from_user(space, &requested, frame->rsi,
				    sizeof(requested))) {
					syscall_error(frame, ERRNO_EFAULT);
					return true;
				}
				requested_pointer = &requested;
			}
			if (frame->rdx != 0) {
				if (!vmm_validate_user_write(space, frame->rdx,
				    sizeof(previous))) {
					syscall_error(frame, ERRNO_EFAULT);
					return true;
				}
				previous_pointer = &previous;
			}
			if (frame->rdi > INT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				return true;
			}
			result = process_signal_mask((int)frame->rdi,
			    requested_pointer, previous_pointer);
			if (result < 0) {
				syscall_error(frame, (uint64_t)-result);
				return true;
			}
			if (previous_pointer != NULL && !vmm_copy_to_user(space,
			    frame->rdx, &previous, sizeof(previous))) {
				syscall_error(frame, ERRNO_EFAULT);
				return true;
			}
			frame->rax = 0;
		}
		return true;
	default:
		return false;
	}
}

struct interrupt_frame *
syscall_proc_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;
	uint64_t error = 0;

	switch (number) {
	case SYSCALL_EXIT:
		return process_exit(frame, frame->rdi);
	case SYSCALL_GETPPID:
		frame->rax = process_current_ppid();
		break;
	case SYSCALL_SETUID:
	case SYSCALL_SETEUID:
	case SYSCALL_SETGID:
	case SYSCALL_SETEGID:
		if (frame->rdi > UINT32_MAX) {
			syscall_error(frame, ERRNO_EINVAL);
		} else {
			int result = number == SYSCALL_SETUID ?
			    process_set_uid((uint32_t)frame->rdi, false) :
			    number == SYSCALL_SETEUID ?
			    process_set_uid((uint32_t)frame->rdi, true) :
			    number == SYSCALL_SETGID ?
			    process_set_gid((uint32_t)frame->rdi, false) :
			    process_set_gid((uint32_t)frame->rdi, true);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_SETRESUID:
	case SYSCALL_SETRESGID:
		{
			int result;
			if (frame->rdi > UINT32_MAX || frame->rsi > UINT32_MAX ||
			    frame->rdx > UINT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = number == SYSCALL_SETRESUID ?
			    process_set_res_uid((uint32_t)frame->rdi,
			    (uint32_t)frame->rsi, (uint32_t)frame->rdx) :
			    process_set_res_gid((uint32_t)frame->rdi,
			    (uint32_t)frame->rsi, (uint32_t)frame->rdx);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UMASK:
		if (frame->rdi > UINT32_MAX)
			syscall_error(frame, ERRNO_EINVAL);
		else
			frame->rax = process_umask((uint32_t)frame->rdi);
		break;
	case SYSCALL_PROCESS_SNAPSHOT:
		{
			struct process_snapshot_entry entries[PROCESS_SNAPSHOT_MAX];
			uint64_t capacity = frame->rsi;
			int count;
			if (capacity == 0 || capacity > PROCESS_SNAPSHOT_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rdi, (size_t)capacity * sizeof(entries[0]))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			count = process_snapshot(entries, (size_t)capacity);
			if (count < 0) {
				syscall_error(frame, (uint64_t)(-count));
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rdi,
			    entries, (size_t)count * sizeof(entries[0]))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = (uint64_t)count;
		}
		break;
	case SYSCALL_PROCESS_INFO:
		{
			struct os_process_info entries[OS_PROCESS_INFO_MAX];
			struct address_space *space = vmm_active_address_space();
			uint64_t capacity = frame->rsi;

			if (capacity == 0 || capacity > OS_PROCESS_INFO_MAX) {
				syscall_error(frame, EINVAL);
				break;
			}
			int count = process_info_snapshot(entries, (size_t)capacity);
			if (count < 0)
				syscall_error(frame, (uint64_t)-count);
			else if (!vmm_copy_to_user(space, frame->rdi, entries,
			    (size_t)count * sizeof(entries[0])))
				syscall_error(frame, EFAULT);
			else
				frame->rax = (uint64_t)count;
		}
		break;
	case SYSCALL_FORK:
		frame = process_fork(frame, &error);
		break;
	case SYSCALL_EXECVE:
		{
			char path[VFS_PATH_MAX];
			char argument_storage[2048];
			const char *arguments[16];
			const char *environment[PROCESS_EXEC_ENV_MAX];
			size_t argc, envc;
			int rc = copy_user_string(frame->rdi, path, sizeof(path));

			if (rc != 0) {
				error = (uint64_t)-rc;
				break;
			}
			if (frame->rsi == 0) {
				error = ERRNO_EFAULT;
				break;
			}
			error = copy_exec_vector(frame->rsi, argument_storage,
			    sizeof(argument_storage), arguments, 16, &argc);
			if (error != 0)
				break;
			if (argc == 0) {
				error = ERRNO_E2BIG;
				break;
			}
			char *storage = kheap_alloc(PROCESS_EXEC_ENV_BYTES_MAX);
			if (storage == NULL) {
				error = ERRNO_ENOMEM;
				break;
			}
			error = copy_exec_vector(frame->rdx, storage,
			    PROCESS_EXEC_ENV_BYTES_MAX, environment, PROCESS_EXEC_ENV_MAX, &envc);
			if (error == 0)
				frame = process_exec_path_env(frame, path, argc, arguments,
				    envc, environment, &error);
			(void)kheap_free(storage);
		}
		break;
	case SYSCALL_WAITPID:
		frame = process_waitpid(frame, &error);
		break;
	case SYSCALL_KILL:
		error = frame->rsi > UINT32_MAX ? ERRNO_EINVAL :
		    process_kill(frame->rdi, (unsigned int)frame->rsi);
		if (error != 0)
			syscall_error(frame, error);
		else
			frame->rax = 0;
		break;
	default:
		syscall_error(frame, ERRNO_ENOSYS);
		break;
	}
	if (error != 0)
		syscall_error(frame, error);
	return frame;
}
