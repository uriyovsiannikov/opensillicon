#include "kernel/elf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put(uint8_t *p, uint64_t value, unsigned width)
{
	for (unsigned i = 0; i < width; i++)
		p[i] = value >> (8 * i);
}

int main(void)
{
	uint8_t image[8192] = {0};
	struct elf_load_plan plan;
	memcpy(image, "\177ELF\2\1\1", 7);
	put(image + 16, 2, 2);
	put(image + 18, 62, 2);
	put(image + 20, 1, 4);
	put(image + 24, 0x400100, 8);
	put(image + 32, 64, 8);
	put(image + 52, 64, 2);
	put(image + 54, 56, 2);
	put(image + 56, 2, 2);
	uint8_t *text = image + 64, *empty = text + 56;
	put(text, 1, 4); /* PT_LOAD, RX, contains ELF headers and entry point. */
	put(text + 4, 5, 4);
	put(text + 16, 0x400000, 8);
	put(text + 32, 4096, 8);
	put(text + 40, 4096, 8);
	put(text + 48, 4096, 8);
	put(empty, 1, 4);
	put(empty + 4, 6, 4);
	put(empty + 8, 0x1801, 8);
	put(empty + 48, 4096, 8);
	assert(elf_image_validate(image, sizeof(image), &plan));
	assert(plan.segment_count == 1 && plan.page_count == 1);
	/* Empty LOAD headers cannot authorize bytes, pages, or an entry point. */
	put(empty + 32, 1, 8);
	assert(!elf_image_validate(image, sizeof(image), &plan));
	put(empty + 32, 0, 8);
	put(empty + 40, 1, 8);
	assert(!elf_image_validate(image, sizeof(image), &plan));
	put(empty + 40, 0, 8);
	put(empty + 8, sizeof(image) + 1, 8);
	assert(!elf_image_validate(image, sizeof(image), &plan));
	put(empty + 8, sizeof(image), 8);
	put(empty + 4, 7, 4);
	assert(!elf_image_validate(image, sizeof(image), &plan));
	put(empty + 4, 6, 4);
	put(image + 24, 0, 8);
	assert(!elf_image_validate(image, sizeof(image), &plan));
	put(image + 24, 0x400100, 8);
	assert(elf_image_validate(image, sizeof(image), &plan));
	puts("ELF empty load segment and rejection checks passed");
	return 0;
}
