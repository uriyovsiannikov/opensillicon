#include "os.h"
#include "abi/syscall.h"

static long
syscall4(uint64_t number, uint64_t first, uint64_t second, uint64_t third,
	uint64_t fourth)
{
	uint64_t result = number;
	register uint64_t r10 __asm__("r10") = fourth;
	unsigned char failed;

	__asm__ volatile ("int $128; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" (first), "S" (second), "d" (third), "r" (r10) : "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

long u_write(uint64_t fd, const void *buffer, size_t length)
{ return syscall4(SYSCALL_WRITE, fd, (uintptr_t)buffer, length, 0); }
long u_read(uint64_t fd, void *buffer, size_t length)
{ return syscall4(SYSCALL_READ, fd, (uintptr_t)buffer, length, 0); }
long u_ioctl(uint64_t fd, uint64_t request, void *argument)
{ return syscall4(SYSCALL_IOCTL, fd, request, (uintptr_t)argument, 0); }
long u_power_control(uint64_t command)
{ return syscall4(SYSCALL_POWER_CONTROL, command, 0, 0, 0); }
long u_kernel_crash(void)
{ return syscall4(SYSCALL_KERNEL_CRASH, 0, 0, 0, 0); }
long u_kldload(const char *path)
{ return syscall4(SYSCALL_KLDLOAD, (uintptr_t)path, 0, 0, 0); }
long u_kldunload(const char *name)
{ return syscall4(SYSCALL_KLDUNLOAD, (uintptr_t)name, 0, 0, 0); }
long u_process_info(struct os_process_info *entries, size_t capacity)
{ return syscall4(SYSCALL_PROCESS_INFO, (uintptr_t)entries, capacity, 0, 0); }
long u_framebuffer_info(struct os_framebuffer_info *info)
{ return syscall4(SYSCALL_FRAMEBUFFER_INFO, (uintptr_t)info, 0, 0, 0); }
long u_framebuffer_present_rect(const struct os_framebuffer_rect *rect)
{ return syscall4(SYSCALL_FRAMEBUFFER_PRESENT_RECT, (uintptr_t)rect, 0, 0, 0); }
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

size_t
u_strlen(const char *string)
{
	size_t length = 0;
	while (string[length] != '\0') length++;
	return length;
}
