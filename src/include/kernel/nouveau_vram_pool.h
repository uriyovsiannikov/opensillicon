/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef KERNEL_NOUVEAU_VRAM_POOL_H
#define KERNEL_NOUVEAU_VRAM_POOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NOUVEAU_VRAM_RANGES 32U
struct nouveau_vram_range { uint64_t start, size; };
struct nouveau_vram_slot {
	struct nouveau_vram_range range;
	uint64_t generation;
	bool reserved;
};
/* Software range accounting only. An initialized pool is noncopyable and
 * caller-serialized. Capacity/exclusions do NOT prove firmware relinquished
 * memory. Only an owning lifecycle may upload to its allocations. */
struct nouveau_vram_pool {
	uint64_t size, next_generation;
	struct nouveau_vram_slot slots[NOUVEAU_VRAM_RANGES];
	bool initialized;
};
struct nouveau_vram_allocation {
	struct nouveau_vram_pool *pool;
	uint64_t generation;
	uint8_t slot;
};
/* Zero-initialize once. Reinitializing a live pool is forbidden. GP102 VGA
 * head 256KiB and VBIOS tail 1MiB are always excluded. Extra exclusions may
 * overlap each other, e.g. scanout overlaps the VGA head. */
bool nouveau_vram_pool_init(struct nouveau_vram_pool *pool, uint64_t size,
	const struct nouveau_vram_range *excluded, size_t count);
bool nouveau_vram_pool_reserve(struct nouveau_vram_pool *pool,
	uint64_t start, uint64_t size);
bool nouveau_vram_pool_alloc(struct nouveau_vram_pool *pool, uint64_t size,
	uint64_t alignment, struct nouveau_vram_allocation *allocation);
bool nouveau_vram_pool_query(struct nouveau_vram_allocation allocation,
	struct nouveau_vram_range *range);
bool nouveau_vram_pool_free(struct nouveau_vram_allocation *allocation);
#endif
