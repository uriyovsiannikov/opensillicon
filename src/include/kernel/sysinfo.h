#ifndef KERNEL_SYSINFO_H
#define KERNEL_SYSINFO_H
#include <stddef.h>
#include "abi/sysctl.h"
enum kernel_info_kind {
	KERNEL_INFO_CPU,
	KERNEL_INFO_MODULES,
	KERNEL_INFO_PCI,
	KERNEL_INFO_NETPCI,
	KERNEL_INFO_AUDIOPCI,
	KERNEL_INFO_USB,
	KERNEL_INFO_DISKS
};
struct kernel_info_report {
	size_t content_length;
	char content[OS_SYSCTL_REPORT_MAX];
};
void kernel_info_set_bootargs(const char *arguments);
const char *kernel_info_bootargs(void);
int kernel_info_snapshot(
    enum kernel_info_kind kind, struct kernel_info_report *report);
#endif
