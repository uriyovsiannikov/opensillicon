#include <errno.h>
#include <sys/resource.h>
#include <sys/sysctl.h>

int
getrlimit(int resource, struct rlimit *limit)
{
	int maximum;
	size_t size = sizeof(maximum);

	if (limit == NULL) {
		errno = EFAULT;
		return -1;
	}
	if (resource != RLIMIT_NOFILE) {
		errno = ENOTSUP;
		return -1;
	}
	if (sysctlbyname("kern.maxfilesperproc", &maximum, &size, NULL, 0) != 0)
		return -1;
	*limit = (struct rlimit){.rlim_cur = maximum, .rlim_max = maximum};
	return 0;
}
