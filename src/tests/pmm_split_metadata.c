#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include "kernel/pmm.h"

int
main(void)
{
	/* The bitmap fits below 1 MiB. The larger DMA pin table must land
	 * in the next usable region, leaving ordinary pages after the bitmap. */
	const uint64_t high = UINT64_C(1) << 32;
	struct limine_memmap_entry low = {
		.base = 0x10000, .length = 0x40000, .type = LIMINE_MEMMAP_USABLE,
	};
	struct limine_memmap_entry middle = {
		.base = 0x100000, .length = 0x400000, .type = LIMINE_MEMMAP_USABLE,
	};
	struct limine_memmap_entry last = {
		.base = high, .length = 0x100000, .type = LIMINE_MEMMAP_USABLE,
	};
	struct limine_memmap_entry *entries[] = { &low, &middle, &last };
	struct limine_memmap_response map = { .entry_count = 3, .entries = entries };
	void *mapping = mmap(NULL, high + last.length, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	assert(mapping != MAP_FAILED);
	struct boot_info info = { .memmap = &map, .hhdm_offset = (uintptr_t)mapping };
	struct bootstrap_allocator boot;
	assert(bootstrap_allocator_init(&boot, &info));
	assert(pmm_init(&info, &boot));
	uint64_t before = pmm_free_pages();
	uint64_t page;
	assert(pmm_alloc_page(&page));
	assert(page >= low.base && page < low.base + low.length);
	assert(pmm_page_is_allocated(page));
	assert(pmm_dma_pin_contiguous(page, 1));
	assert(!pmm_free_page(page));
	assert(pmm_dma_unpin_contiguous(page, 1));
	assert(pmm_free_page(page));
	assert(pmm_alloc_contiguous(2, 1, low.base + low.length, &page));
	assert(pmm_free_contiguous(page, 2));
	assert(pmm_free_pages() == before);
	assert(!pmm_free_page(low.base));
	assert(!pmm_free_page(middle.base));
	assert(munmap(mapping, high + last.length) == 0);
	puts("PMM split metadata: PASS");
}
