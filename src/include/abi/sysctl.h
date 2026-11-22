#ifndef ABI_SYSCTL_H
#define ABI_SYSCTL_H

#define CTL_MAXNAME 2U
#define CTL_KERN 1
#define CTL_VM 2
#define CTL_HW 6

enum os_sysctl_kind {
	OS_SYSCTL_STRING,
	OS_SYSCTL_INT32,
	OS_SYSCTL_UINT64
};

/* One registry for the numeric MIB, public names and value types.
 * vm.freepages is a native 64-bit count of currently free physical frames. */
#define OS_SYSCTL_NODES(X) \
	X(KERN_OSTYPE, CTL_KERN, 1, "kern.ostype", OS_SYSCTL_STRING) \
	X(KERN_OSRELEASE, CTL_KERN, 2, "kern.osrelease", OS_SYSCTL_STRING) \
	X(KERN_VERSION, CTL_KERN, 4, "kern.version", OS_SYSCTL_STRING) \
	X(KERN_HOSTNAME, CTL_KERN, 10, "kern.hostname", OS_SYSCTL_STRING) \
	X(VM_FREEPAGES, CTL_VM, 1, "vm.freepages", OS_SYSCTL_UINT64) \
	X(HW_MACHINE, CTL_HW, 1, "hw.machine", OS_SYSCTL_STRING) \
	X(HW_NCPU, CTL_HW, 3, "hw.ncpu", OS_SYSCTL_INT32) \
	X(HW_BYTEORDER, CTL_HW, 4, "hw.byteorder", OS_SYSCTL_INT32) \
	X(HW_PHYSMEM, CTL_HW, 5, "hw.physmem", OS_SYSCTL_UINT64) \
	X(HW_PAGESIZE, CTL_HW, 7, "hw.pagesize", OS_SYSCTL_INT32) \
	X(HW_MACHINE_ARCH, CTL_HW, 11, "hw.machine_arch", OS_SYSCTL_STRING)

enum os_sysctl_leaf {
#define OS_SYSCTL_LEAF(symbol, group, value, name, kind) symbol = value,
	OS_SYSCTL_NODES(OS_SYSCTL_LEAF)
#undef OS_SYSCTL_LEAF
};

#endif
