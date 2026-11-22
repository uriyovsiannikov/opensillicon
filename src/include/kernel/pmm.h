#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "limine.h"
#include "kernel/boot.h"

#define PMM_PAGE_SIZE 4096ULL

bool pmm_init(const struct boot_info *info,
	struct bootstrap_allocator *bootstrap);
bool pmm_alloc_page(uint64_t *physical_address);
bool pmm_free_page(uint64_t physical_address);
bool pmm_alloc_contiguous(size_t page_count, size_t alignment_pages,
	uint64_t maximum_address_exclusive, uint64_t *physical_address);
bool pmm_free_contiguous(uint64_t physical_address, size_t page_count);
bool pmm_dma_pin_contiguous(uint64_t physical_address, size_t page_count);
bool pmm_dma_unpin_contiguous(uint64_t physical_address, size_t page_count);
bool pmm_page_is_allocated(uint64_t physical_address);
uint64_t pmm_total_pages(void);
/* Full usable frames, excluding reserved memory and physical address holes. */
uint64_t pmm_usable_pages(void);
uint64_t pmm_free_pages(void);

#endif
