#ifndef ABI_SYSCTL_H
#define ABI_SYSCTL_H

#define CTL_MAXNAME 2U
#define OS_SYSCTL_REPORT_MAX 16384U
#define CTL_KERN 1
#define CTL_VM 2
#define CTL_HW 6
#define CTL_NET 4

enum os_sysctl_kind {
	OS_SYSCTL_STRING,
	OS_SYSCTL_INT32,
	OS_SYSCTL_UINT64,
	OS_SYSCTL_BYTES
};

/* One registry for the numeric MIB, public names and value types.
 * vm.freepages is a native 64-bit count of currently free physical frames. */
#define OS_SYSCTL_NODES(X) \
	X(KERN_OSTYPE, CTL_KERN, 1, "kern.ostype", OS_SYSCTL_STRING) \
	X(KERN_OSRELEASE, CTL_KERN, 2, "kern.osrelease", OS_SYSCTL_STRING) \
	X(KERN_VERSION, CTL_KERN, 4, "kern.version", OS_SYSCTL_STRING) \
	X(KERN_HOSTNAME, CTL_KERN, 10, "kern.hostname", OS_SYSCTL_STRING) \
	X(KERN_PROC_ALL, CTL_KERN, 14, "kern.proc.all", OS_SYSCTL_BYTES) \
	X(KERN_MAXFILESPERPROC, CTL_KERN, 28, "kern.maxfilesperproc", OS_SYSCTL_INT32) \
	X(KERN_BOOTARGS, CTL_KERN, 38, "kern.bootargs", OS_SYSCTL_STRING) \
	X(KERN_DRIVERAPI, CTL_KERN, 39, "kern.driverapi", OS_SYSCTL_INT32) \
	X(KERN_MODULES, CTL_KERN, 40, "kern.modules", OS_SYSCTL_STRING) \
	X(KERN_UPTIME_NS, CTL_KERN, 41, "kern.uptime_ns", OS_SYSCTL_UINT64) \
	X(HW_CPUINFO, CTL_HW, 20, "hw.cpuinfo", OS_SYSCTL_STRING) \
	X(HW_PCI, CTL_HW, 21, "hw.pci", OS_SYSCTL_STRING) \
	X(HW_USB, CTL_HW, 22, "hw.usb", OS_SYSCTL_STRING) \
	X(HW_DISKS, CTL_HW, 23, "hw.disks", OS_SYSCTL_STRING) \
	X(HW_NETPCI, CTL_HW, 24, "hw.netpci", OS_SYSCTL_STRING) \
	X(HW_AUDIOPCI, CTL_HW, 25, "hw.audiopci", OS_SYSCTL_STRING) \
	X(NET_INTERFACE, CTL_NET, 1, "net.interface", OS_SYSCTL_STRING) \
	X(NET_IPV4_CONFIG, CTL_NET, 2, "net.inet.ip.config", OS_SYSCTL_BYTES) \
	X(NET_IPV4_DNS, CTL_NET, 3, "net.inet.ip.dns", OS_SYSCTL_BYTES) \
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
