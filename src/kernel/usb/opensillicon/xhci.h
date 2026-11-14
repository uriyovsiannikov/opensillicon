#ifndef OPEN_SILLICON_XHCI_H
#define OPEN_SILLICON_XHCI_H

#include <stdbool.h>
#include <stdint.h>

struct pci_device_info;

/* Attach a BSP-owned controller for the lifetime of the USB host stack. */
bool xhci_attach(const struct pci_device_info *device, unsigned int controller);

void xhci_input_diagnostics(void (*write_value)(const char *, uint64_t, bool));

#endif
