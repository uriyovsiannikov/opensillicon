#include <sys/opensillicon.h>
#include "abi/syscall.h"
#include "syscall.h"

extern long __os_native_call(long, long, long, long, long, long, long);

int
os_module_load(const char *path)
{
	return __syscall_ret(__os_native_call(SYSCALL_KLDLOAD, (long)path, 0, 0, 0, 0, 0));
}

int
os_module_unload(const char *name)
{
	return __syscall_ret(__os_native_call(SYSCALL_KLDUNLOAD, (long)name, 0, 0, 0, 0, 0));
}

int
os_power_control(unsigned int command)
{
	return __syscall_ret(__os_native_call(SYSCALL_POWER_CONTROL, command, 0, 0, 0, 0, 0));
}

int
os_framebuffer_info(struct os_framebuffer_info *info)
{
	return __syscall_ret(__os_native_call(SYSCALL_FRAMEBUFFER_INFO, (long)info, 0, 0, 0, 0, 0));
}

int
os_framebuffer_present_rect(const struct os_framebuffer_rect *rect)
{
	return __syscall_ret(__os_native_call(SYSCALL_FRAMEBUFFER_PRESENT_RECT, (long)rect, 0, 0, 0, 0, 0));
}
