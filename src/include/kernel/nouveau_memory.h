/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_MEMORY_H
#define KERNEL_NOUVEAU_MEMORY_H
#include "kernel/nouveau_display.h"
#include "kernel/dma.h"
#define NOUVEAU_INSTANCE_SIZE 0x10000U
#define NOUVEAU_RAMHT_SLOTS 512U
#define NOUVEAU_SURFACE_OBJECT 0x1000U
#define NOUVEAU_NOTIFIER_OBJECT 0x1020U
#define NOUVEAU_NOTIFIER_OFFSET 0xf000U
#define NOUVEAU_SURFACE_HANDLE 0xf0000001U
#define NOUVEAU_NOTIFIER_HANDLE 0xf0000000U
/* Total physical capacity, not the amount of available VRAM. */
/* Relate a boot CPU physical span to a sized PCI aperture. This does not
 * translate BAR1 virtual addresses through the GPU MMU. */
bool nouveau_framebuffer_bar_offset(const struct pci_bar_resource *bar,
	uint64_t physical, uint64_t size, uint64_t *offset);
bool nouveau_gp102_vram_size(uint32_t reg, uint64_t *bytes);
bool nouveau_vram_dma_object(uint32_t words[6], uint64_t start,
	uint64_t limit, bool small_pages);
/* Fixed 4KiB display RAMHT, DMA channels 0..8. Parent-relative object offset. */
bool nouveau_ramht_insert(uint32_t *image, unsigned channel, uint32_t handle,
	uint32_t object_offset);
bool nouveau_ramht_lookup(const uint32_t *image, unsigned channel,
	uint32_t handle, uint32_t *object_offset);
/* CPU staging image only. Notifier addresses are instance-relative until
 * explicitly relocated into a caller-owned VRAM allocation. */
bool nouveau_instance_prepare(uint32_t *image, size_t size, uint64_t vram,
	unsigned head);
/* Caller must OWN this VRAM range; range validation does not allocate VRAM.
 * Never pass a host physical/DMA address here. */
bool nouveau_instance_relocate(uint32_t *image, size_t size, uint64_t vram,
	uint64_t owned_vram_base, size_t owned_size, uint32_t *instance_register);
struct nouveau_display_staging {
	struct dma_buffer instance, push;
	uint32_t push_descriptor;
	uint16_t staged_words;
	uint8_t head;
};
enum nouveau_staging_status {
	NOUVEAU_STAGING_OK, NOUVEAU_STAGING_INVALID, NOUVEAU_STAGING_UNAVAILABLE,
	NOUVEAU_STAGING_ALLOCATION_FAILED, NOUVEAU_STAGING_CLEANUP_FAILED
};
/* Allocations remain CPU-owned: no doorbell, channel binding or GPU writes. */
enum nouveau_staging_status nouveau_display_staging_create(
	uint64_t vram, const struct nouveau_display_plan *plan,
	struct nouveau_display_staging *staging);
/* Append a validated surface block to the existing CPU-owned timing buffer.
 * No device ownership transfer or submission. Only one append is accepted. */
bool nouveau_display_staging_surface(struct nouveau_display_staging *staging,
	uint64_t candidate, uint64_t vram_size, const struct os_framebuffer_info *fb);
bool nouveau_display_staging_release(struct nouveau_display_staging *staging);
#endif
