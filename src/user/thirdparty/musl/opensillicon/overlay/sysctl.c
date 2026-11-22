#include <errno.h>
#include <string.h>
#include <sys/sysctl.h>
#include "abi/syscall.h"
#include "syscall.h"

extern long __os_native_call(long, long, long, long, long, long, long);

int sysctl(const int *name, unsigned int count, void *old, size_t *length,
	const void *replacement, size_t replacement_length)
{
	return __syscall_ret(__os_native_call(SYSCALL_SYSCTL, (long)name, count,
	    (long)old, (long)length, (long)replacement, replacement_length));
}

int sysctlnametomib(const char *name, int *mib, size_t *count)
{
	static const struct {
		const char *name;
		int mib[2];
	} nodes[] = {
#define NODE(symbol, group, value, text, kind) {text, {group, symbol}},
		OS_SYSCTL_NODES(NODE)
#undef NODE
	};
	if (name == NULL || count == NULL) {
		errno = EFAULT;
		return -1;
	}
	for (size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
		if (strcmp(name, nodes[i].name))
			continue;
		size_t capacity = *count;
		*count = 2;
		if (mib != NULL) {
			if (capacity < 2) {
				errno = ENOMEM;
				return -1;
			}
			memcpy(mib, nodes[i].mib, sizeof(nodes[i].mib));
		}
		return 0;
	}
	errno = ENOENT;
	return -1;
}

int sysctlbyname(const char *name, void *old, size_t *length,
	const void *replacement, size_t replacement_length)
{
	int mib[CTL_MAXNAME];
	size_t count = CTL_MAXNAME;
	if (sysctlnametomib(name, mib, &count) < 0)
		return -1;
	return sysctl(mib, count, old, length, replacement, replacement_length);
}
