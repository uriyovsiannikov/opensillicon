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
/* BAR VM instances have a separate lifecycle and layout from display RAMHT. */
void nouveau_firmware_bar_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar, unsigned pci_index,
	uint32_t context_register, uint64_t vram_size);
enum nouveau_scanout_match {
	NOUVEAU_SCANOUT_SAMPLE_MATCH, NOUVEAU_SCANOUT_MISMATCH,
	NOUVEAU_SCANOUT_FLAT, NOUVEAU_SCANOUT_UNSTABLE,
	NOUVEAU_SCANOUT_IO_FAILED, NOUVEAU_SCANOUT_INVALID
};
struct nouveau_scanout_reader {
	void *context;
	bool (*cpu)(void *context, uint64_t offset, uint32_t *words, size_t count);
	bool (*gpu)(void *context, uint64_t address, uint32_t *words, size_t count);
};
/* Compare 128 distributed 64-byte samples, bracketed by CPU reads to reject
 * changing pixels. Uniform data cannot establish an address correspondence.
 * A match is sampling evidence, not ownership or proof of the whole range. */
enum nouveau_scanout_match nouveau_scanout_compare(const struct nouveau_scanout_reader *io,
	uint64_t vram_size, uint64_t candidate, uint64_t framebuffer_size);
void nouveau_scanout_probe_log(const struct pci_device_lease *claim,
	const struct pci_bar_resource *bar, uint32_t bar3_context,
	uint64_t vram_size, uint64_t bar_offset, uint64_t framebuffer_size);
#endif
