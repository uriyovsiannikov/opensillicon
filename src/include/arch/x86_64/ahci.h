#ifndef ARCH_X86_64_AHCI_H
#define ARCH_X86_64_AHCI_H

#include <stdbool.h>

#include "arch/x86_64/pci.h"
#include "kernel/block.h"

/* Discover all supported SATA disks, independently of the root mount. */
size_t ahci_discover(const struct pci_device_info *controller);

#endif
