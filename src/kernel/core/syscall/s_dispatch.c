#include "kernel/hda_audio.h"
#include "syscall_internal.h"

#include "arch/x86_64/user.h"
#include "kernel/log.h"
#include "kernel/network_service.h"
#include "kernel/smp.h"
#include "kernel/usb_host.h"
#include "kernel/vmm.h"

#define SYSCALL_VECTOR 128U

struct syscall_route {
	bool (*local)(struct interrupt_frame **);
	struct interrupt_frame *(*bsp)(struct interrupt_frame *);
};

/* Local handlers use synchronized process state and may run on either CPU
 * path. Descriptor I/O can decline the local path and use the BSP handler. */
static const struct syscall_route syscall_routes[] = {
	/* Device */
	[SYSCALL_FRAMEBUFFER_INFO] = { .bsp = syscall_device_bsp },
	[SYSCALL_FRAMEBUFFER_PRESENT] = { .bsp = syscall_device_bsp },
	[SYSCALL_FRAMEBUFFER_PRESENT_RECT] = { .bsp = syscall_device_bsp },
	[SYSCALL_KEYBOARD_POLL] = { .bsp = syscall_device_bsp },
	[SYSCALL_MOUSE_POLL] = { .bsp = syscall_device_bsp },
	[SYSCALL_PTY_PAIR] = { .bsp = syscall_device_bsp },
	[SYSCALL_IOCTL] = { .bsp = syscall_device_bsp },
	/* File */
	[SYSCALL_MOUNT] = { .bsp = syscall_file_bsp },
	[SYSCALL_UNMOUNT] = { .bsp = syscall_file_bsp },
	[SYSCALL_FSYNC] = { .bsp = syscall_file_bsp },
	[SYSCALL_SYNC] = { .bsp = syscall_file_bsp },
	[SYSCALL_CHMOD] = { .bsp = syscall_file_bsp },
	[SYSCALL_MKDIR] = { .bsp = syscall_file_bsp },
	[SYSCALL_UNLINK] = { .bsp = syscall_file_bsp },
	[SYSCALL_ACCESS] = { .bsp = syscall_file_bsp },
	[SYSCALL_FCHMOD] = { .bsp = syscall_file_bsp },
	[SYSCALL_CHOWN] = { .bsp = syscall_file_bsp },
	[SYSCALL_FCHOWN] = { .bsp = syscall_file_bsp },
	[SYSCALL_CHDIR] = { .bsp = syscall_file_bsp },
	[SYSCALL_GETCWD] = { .bsp = syscall_file_bsp },
	[SYSCALL_OPENAT] = { .bsp = syscall_file_bsp },
	[SYSCALL_OPEN] = { .bsp = syscall_file_bsp },
	[SYSCALL_FSTAT] = { .bsp = syscall_file_bsp },
	[SYSCALL_FSTATAT] = { .bsp = syscall_file_bsp },
	[SYSCALL_STAT] = { .bsp = syscall_file_bsp },
	[SYSCALL_GETDENTS] = { .bsp = syscall_file_bsp },
	[SYSCALL_READDIR] = { .bsp = syscall_file_bsp },
	/* Descriptor I/O */
	[SYSCALL_READV] = { .local = syscall_io_local, .bsp = syscall_io_bsp },
	[SYSCALL_WRITEV] = { .local = syscall_io_local, .bsp = syscall_io_bsp },
	[SYSCALL_READ] = { .local = syscall_io_local, .bsp = syscall_io_bsp },
	[SYSCALL_WRITE] = { .local = syscall_io_local, .bsp = syscall_io_bsp },
	[SYSCALL_CLOSE] = { .local = syscall_io_local, .bsp = syscall_io_bsp },
	[SYSCALL_PIPE2] = { .local = syscall_io_local },
	[SYSCALL_PIPE] = { .local = syscall_io_local },
	[SYSCALL_FCNTL] = { .bsp = syscall_io_bsp },
	[SYSCALL_DUP3] = { .bsp = syscall_io_bsp },
	[SYSCALL_DUP2] = { .bsp = syscall_io_bsp },
	[SYSCALL_DUP] = { .bsp = syscall_io_bsp },
	[SYSCALL_LSEEK] = { .bsp = syscall_io_bsp },
	[SYSCALL_POLL] = { .bsp = syscall_io_bsp },
	[SYSCALL_SELECT] = { .bsp = syscall_io_bsp },
	/* Memory */
	[SYSCALL_MMAP] = { .local = syscall_memory_local },
	[SYSCALL_MUNMAP] = { .local = syscall_memory_local },
	[SYSCALL_MPROTECT] = { .local = syscall_memory_local },
	[SYSCALL_SHM_CREATE] = { .local = syscall_memory_local },
	[SYSCALL_SHM_DETACH] = { .local = syscall_memory_local },
	/* Internet sockets and network configuration */
	[SYSCALL_SOCKET_KIND] = { .bsp = syscall_net_bsp },
	[SYSCALL_UDP_SOCKET] = { .bsp = syscall_net_bsp },
	[SYSCALL_UDP_BIND] = { .bsp = syscall_net_bsp },
	[SYSCALL_UDP_CONNECT] = { .bsp = syscall_net_bsp },
	[SYSCALL_UDP_SEND] = { .bsp = syscall_net_bsp },
	[SYSCALL_UDP_SENDTO] = { .bsp = syscall_net_bsp },
	[SYSCALL_UDP_RECVFROM] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_SOCKET] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_BIND] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_CONNECT] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_LISTEN] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_ACCEPT] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_SHUTDOWN] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_GETSOCKOPT] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_SETSOCKOPT] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_GETSOCKNAME] = { .bsp = syscall_net_bsp },
	[SYSCALL_TCP_GETPEERNAME] = { .bsp = syscall_net_bsp },
	[SYSCALL_UDP_GETSOCKNAME] = { .bsp = syscall_net_bsp },
	[SYSCALL_UDP_GETPEERNAME] = { .bsp = syscall_net_bsp },
	[SYSCALL_NET_GET_DNS_SERVER] = { .bsp = syscall_net_bsp },
	[SYSCALL_NET_GET_IPV4_CONFIG] = { .bsp = syscall_net_bsp },
	[SYSCALL_ICMP_ECHO] = { .bsp = syscall_net_bsp },
	/* Processes and signals */
	[SYSCALL_GET_ABI_VERSION] = { .local = syscall_proc_local },
	[SYSCALL_GETPID] = { .local = syscall_proc_local },
	[SYSCALL_GETCREDENTIALS] = { .local = syscall_proc_local },
	[SYSCALL_SET_FS_BASE] = { .local = syscall_proc_local },
	[SYSCALL_SIGACTION] = { .local = syscall_proc_local },
	[SYSCALL_SIGRETURN] = { .local = syscall_proc_local },
	[SYSCALL_SIGPROCMASK] = { .local = syscall_proc_local },
	[SYSCALL_EXIT] = { .bsp = syscall_proc_bsp },
	[SYSCALL_GETPPID] = { .bsp = syscall_proc_bsp },
	[SYSCALL_SETUID] = { .bsp = syscall_proc_bsp },
	[SYSCALL_SETEUID] = { .bsp = syscall_proc_bsp },
	[SYSCALL_SETGID] = { .bsp = syscall_proc_bsp },
	[SYSCALL_SETEGID] = { .bsp = syscall_proc_bsp },
	[SYSCALL_SETRESUID] = { .bsp = syscall_proc_bsp },
	[SYSCALL_SETRESGID] = { .bsp = syscall_proc_bsp },
	[SYSCALL_UMASK] = { .bsp = syscall_proc_bsp },
	[SYSCALL_PROCESS_SNAPSHOT] = { .bsp = syscall_proc_bsp },
	[SYSCALL_PROCESS_INFO] = { .bsp = syscall_proc_bsp },
	[SYSCALL_FORK] = { .bsp = syscall_proc_bsp },
	[SYSCALL_EXECVE] = { .bsp = syscall_proc_bsp },
	[SYSCALL_WAITPID] = { .bsp = syscall_proc_bsp },
	[SYSCALL_KILL] = { .bsp = syscall_proc_bsp },
	/* System */
	[SYSCALL_POWER_CONTROL] = { .bsp = syscall_system_bsp },
	[SYSCALL_KERNEL_CRASH] = { .bsp = syscall_system_bsp },
	[SYSCALL_KLDLOAD] = { .bsp = syscall_system_bsp },
	[SYSCALL_KLDUNLOAD] = { .bsp = syscall_system_bsp },
	/* Time */
	[SYSCALL_SLEEP_TICKS] = { .local = syscall_time_local },
	[SYSCALL_NANOSLEEP] = { .local = syscall_time_local },
	[SYSCALL_CLOCK_GETTIME] = { .local = syscall_time_local },
	/* Unix */
	[SYSCALL_UNIX_SOCKET] = { .bsp = syscall_unix_bsp },
	[SYSCALL_UNIX_BIND] = { .bsp = syscall_unix_bsp },
	[SYSCALL_UNIX_CONNECT] = { .bsp = syscall_unix_bsp },
	[SYSCALL_UNIX_LISTEN] = { .bsp = syscall_unix_bsp },
	[SYSCALL_UNIX_ACCEPT] = { .bsp = syscall_unix_bsp },
	[SYSCALL_UNIX_SHUTDOWN] = { .bsp = syscall_unix_bsp },
	[SYSCALL_UNIX_GETSOCKOPT] = { .bsp = syscall_unix_bsp },
	[SYSCALL_UNIX_SETSOCKOPT] = { .bsp = syscall_unix_bsp },
};

int
copy_user_string(uint64_t address, char *destination, size_t capacity)
{
	struct address_space *space = vmm_active_address_space();
	size_t i;

	if (destination == NULL || capacity == 0)
		return -EFAULT;
	for (i = 0; i < capacity; i++) {
		if (address > UINT64_MAX - i ||
		    !vmm_copy_from_user(space, &destination[i], address + i, 1))
			return -EFAULT;
		if (destination[i] == '\0')
			return 0;
	}
	return -(int)ENAMETOOLONG;
}

void
syscall_error(struct interrupt_frame *frame, uint64_t error)
{
	frame->rax = error;
	frame->rflags |= 1U;
}

static bool
syscall_private_memory(uint64_t number)
{
	return number == SYSCALL_MMAP || number == SYSCALL_MUNMAP ||
	    number == SYSCALL_MPROTECT;
}

struct interrupt_frame *
syscall_dispatch(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;
	const struct syscall_route *route = number <
	    sizeof(syscall_routes) / sizeof(syscall_routes[0]) ?
	    &syscall_routes[number] : NULL;
	struct smp_cpu *cpu = smp_current_cpu();

	/* Defer calls that need BSP-owned services until the AP releases the process. */
	if (cpu != NULL && !cpu->bsp) {
		if ((frame->cs & 3U) == 3U && cpu->current_process != NULL) {
			uint64_t flags = frame->rflags;
			frame->rflags &= ~1ULL;
			bool private_memory = syscall_private_memory(number);
			bool shared_memory = number == SYSCALL_SHM_CREATE ||
			    number == SYSCALL_SHM_DETACH;
#if defined(KERNEL_TEST_SHM_ALLOC_FAILURE) || defined(KERNEL_TEST_SHM_LIFECYCLE) || defined(KERNEL_TEST_SHM_FORK_FAILURE)
			if (shared_memory)
				process_ap_stop(frame, PROCESS_SLICE_SYSCALL);
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
			if (private_memory)
				process_ap_stop(frame, PROCESS_SLICE_SYSCALL);
#endif
			process_channel_wait_begin();
			if (route != NULL && route->local != NULL &&
			    route->local(&frame)) {
				if ((number == SYSCALL_SIGACTION ||
				    number == SYSCALL_SIGPROCMASK) &&
				    (frame->rflags & 1U) == 0)
					__atomic_store_n(&cpu->user_signal_calls, true, __ATOMIC_RELAXED);
				if (shared_memory && (frame->rflags & 1U) == 0)
					__atomic_store_n(&cpu->user_shared_memory_calls, true, __ATOMIC_RELAXED);
				if (private_memory && (frame->rflags & 1U) == 0)
					__atomic_store_n(&cpu->user_private_memory_calls, true, __ATOMIC_RELAXED);
				__atomic_add_fetch(&cpu->user_local_syscalls, 1, __ATOMIC_RELAXED);
				return frame;
			}
			frame->rflags = flags;
			process_ap_stop(frame, PROCESS_SLICE_SYSCALL);
		}
		frame->rflags &= ~1ULL;
		if (cpu->user_entry_probe_active && (frame->cs & 3U) == 3U) {
			if (number == SYSCALL_GET_ABI_VERSION) {
				frame->rax = SYSCALL_ABI_VERSION;
				return frame;
			}
			if (number == SYSCALL_EXIT)
				user_return_to_kernel(frame->rdi);
		}
		syscall_error(frame, ENOSYS);
		return frame;
	}
	if ((frame->cs & 3U) != 3U || current_fd_table() == NULL) {
		syscall_error(frame, ENOSYS);
		return frame;
	}

	/* Generic syscall safe point: network progress is not tied to UDP calls. */
	(void)network_service_request(NETWORK_SERVICE_POLL_RX);
	(void)network_service_run(NETWORK_SERVICE_MAX_EVENTS, NULL);
	usb_host_poll();
				hda_audio_poll();
	process_syscall_retry_begin();
	frame->rflags &= ~1ULL;
	process_channel_wait_begin();
	if (route != NULL && route->local != NULL && route->local(&frame))
		return frame;
	if (route != NULL && route->bsp != NULL)
		return route->bsp(frame);
	syscall_error(frame, ENOSYS);
	return frame;
}

void
syscall_init(void)
{
	if (!interrupt_register_handler(SYSCALL_VECTOR, syscall_dispatch)) {
		klog("OpenSillicon: syscall initialization failed\n");
		kernel_halt();
	}
}
