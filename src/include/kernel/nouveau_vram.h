/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_VRAM_H
#define KERNEL_NOUVEAU_VRAM_H
#include "kernel/nouveau_memory.h"
struct nouveau_pramin_io {
	void *context;
	bool (*read32)(void *context, uint32_t reg, uint32_t *value);
	/* The only permitted write is the PRAMIN window register 0x1700. */
	bool (*write32)(void *context, uint32_t reg, uint32_t value);
};
enum nouveau_vram_status {
	NOUVEAU_VRAM_OK, NOUVEAU_VRAM_INVALID, NOUVEAU_VRAM_IO_FAILED,
	NOUVEAU_VRAM_RESTORE_FAILED
};
/* Caller exclusively owns GPU transport. Reads <=64KiB, including ranges
 * crossing a 1MiB PRAMIN window. Partial output is invalid on any failure.
 * No GPU memory writes. Restores the exact saved register with read-back. */
enum nouveau_vram_status nouveau_vram_read(const struct nouveau_pramin_io *io,
	uint64_t vram_size, uint64_t address, uint32_t *destination, size_t length);
void nouveau_firmware_instance_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar, const struct nouveau_display_snapshot *snapshot,
	uint64_t vram_size);
#endif
