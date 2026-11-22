#include "os.h"
#include "abi/syscall.h"

static long
syscall3(uint64_t number, uint64_t first, uint64_t second, uint64_t third)
{
	uint64_t result = number;
	unsigned char failed;

	__asm__ volatile ("int $128; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" (first), "S" (second), "d" (third) : "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

long u_write(uint64_t fd, const void *buffer, size_t length)
{ return syscall3(SYSCALL_WRITE, fd, (uintptr_t)buffer, length); }
long u_read(uint64_t fd, void *buffer, size_t length)
{ return syscall3(SYSCALL_READ, fd, (uintptr_t)buffer, length); }
long u_open(const char *path, uint64_t flags)
{ return syscall3(SYSCALL_OPEN, (uintptr_t)path, flags, 0); }
long u_open_mode(const char *path, uint64_t flags, uint32_t mode)
{ return syscall3(SYSCALL_OPEN, (uintptr_t)path, flags, mode); }
long u_mkdir(const char *path, uint32_t mode)
{ return syscall3(SYSCALL_MKDIR, (uintptr_t)path, mode, 0); }
long u_unlink(const char *path, int directory)
{ return syscall3(SYSCALL_UNLINK, (uintptr_t)path, (uint64_t)(directory != 0), 0); }
long u_close(uint64_t fd) { return syscall3(SYSCALL_CLOSE, fd, 0, 0); }
long u_stat_path(const char *path, struct u_stat *status)
{ return syscall3(SYSCALL_STAT, (uintptr_t)path, (uintptr_t)status, 0); }
long u_readdir(uint64_t fd, struct u_dirent *entry)
{ return syscall3(SYSCALL_READDIR, fd, (uintptr_t)entry, 0); }
long u_ioctl(uint64_t fd, uint64_t request, void *argument)
{ return syscall3(SYSCALL_IOCTL, fd, request, (uintptr_t)argument); }
long u_network_ipv4_config(struct u_ipv4_config *config)
{ return syscall3(SYSCALL_NET_GET_IPV4_CONFIG, (uintptr_t)config, 0, 0); }
long u_network_dns_server(uint8_t address[4])
{ return syscall3(SYSCALL_NET_GET_DNS_SERVER, (uintptr_t)address, 0, 0); }
long u_icmp_echo(struct u_icmp_echo_request *request)
{ return syscall3(SYSCALL_ICMP_ECHO, (uintptr_t)request, 0, sizeof(*request)); }
long u_fork(void) { return syscall3(SYSCALL_FORK, 0, 0, 0); }
long u_waitpid(uint64_t pid, int *status)
{ return u_waitpid_options(pid, status, 0); }
long u_waitpid_options(uint64_t pid, int *status, uint64_t options)
{ return syscall3(SYSCALL_WAITPID, pid, (uintptr_t)status, options); }
long u_sleep_ticks(uint64_t ticks) { return syscall3(SYSCALL_SLEEP_TICKS, ticks, 0, 0); }
long u_power_control(uint64_t command)
{ return syscall3(SYSCALL_POWER_CONTROL, command, 0, 0); }
long u_kernel_crash(void)
{ return syscall3(SYSCALL_KERNEL_CRASH, 0, 0, 0); }
long u_kldload(const char *path)
{ return syscall3(SYSCALL_KLDLOAD, (uintptr_t)path, 0, 0); }
long u_kldunload(const char *name)
{ return syscall3(SYSCALL_KLDUNLOAD, (uintptr_t)name, 0, 0); }
long u_chdir(const char *path)
{ return syscall3(SYSCALL_CHDIR, (uintptr_t)path, 0, 0); }
long u_getcwd(char *buffer, size_t capacity)
{ return syscall3(SYSCALL_GETCWD, (uintptr_t)buffer, capacity, 0); }
long u_getpid(void) { return syscall3(SYSCALL_GETPID, 0, 0, 0); }
long u_process_snapshot(struct u_process_info *entries, size_t capacity)
{ return syscall3(SYSCALL_PROCESS_SNAPSHOT, (uintptr_t)entries, capacity, 0); }
long u_framebuffer_info(struct os_framebuffer_info *info)
{ return syscall3(SYSCALL_FRAMEBUFFER_INFO, (uintptr_t)info, 0, 0); }
long u_framebuffer_present_rect(const struct os_framebuffer_rect *rect)
{ return syscall3(SYSCALL_FRAMEBUFFER_PRESENT_RECT, (uintptr_t)rect, 0, 0); }
long u_kill(uint64_t pid, uint64_t signo)
{ return syscall3(SYSCALL_KILL, pid, signo, 0); }
_Noreturn void u_exit(uint64_t status)
{
	(void)syscall3(SYSCALL_EXIT, status, 0, 0);
	__asm__ volatile ("ud2");
	__builtin_unreachable();
}

void u_putn(const char *string, size_t length) { (void)u_write(1, string, length); }
void u_puts(const char *string) { u_putn(string, u_strlen(string)); }

void
u_put_u64(uint64_t value)
{
	char digits[20];
	size_t count = 0;
	do {
		digits[count++] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0);
	while (count != 0)
		u_putn(&digits[--count], 1);
}
