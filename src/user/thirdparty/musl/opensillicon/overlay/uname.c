#include <errno.h>
#include <stddef.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

int uname(struct utsname *result)
{
	if (result == NULL) {
		errno = EFAULT;
		return -1;
	}
	struct utsname value = {0};
	struct {
		int group, leaf;
		char *destination;
		size_t capacity;
	} fields[] = {
		{CTL_KERN, KERN_OSTYPE, value.sysname, sizeof(value.sysname)},
		{CTL_KERN, KERN_HOSTNAME, value.nodename, sizeof(value.nodename)},
		{CTL_KERN, KERN_OSRELEASE, value.release, sizeof(value.release)},
		{CTL_KERN, KERN_VERSION, value.version, sizeof(value.version)},
		{CTL_HW, HW_MACHINE, value.machine, sizeof(value.machine)}
	};
	for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		int mib[] = {fields[i].group, fields[i].leaf};
		if (sysctl(mib, 2, fields[i].destination, &fields[i].capacity, NULL, 0) < 0)
			return -1;
	}
	*result = value;
	return 0;
}
