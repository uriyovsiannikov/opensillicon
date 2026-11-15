#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define PAGE 4096UL
#define SMALL_MAGIC UINT64_C(0x4c49424f53534d41)
#define LARGE_MAGIC UINT64_C(0x4c49424f534c4152)
#define BLOCK_MAGIC UINT64_C(0x4c49424f53424c4b)
#define CLASS_COUNT 7U

/* The native libc currently has no threads. As with the previous allocator,
 * allocation from an asynchronous signal handler is not supported. */
struct block {
	uint64_t magic;
	size_t size;
};

struct slab {
	uint64_t magic;
	size_t class_index;
	size_t live;
	struct block *free;
	struct slab *previous;
	struct slab *next;
	uint64_t reserved[2];
};

struct large {
	uint64_t magic;
	size_t pages;
	struct block block;
};

_Static_assert(sizeof(struct block) == 16, "aligned allocation header");
_Static_assert(sizeof(struct slab) == 64, "aligned slab header");
_Static_assert(sizeof(struct large) == 32, "aligned large header");

static struct slab *available[CLASS_COUNT];
static struct slab *spare[CLASS_COUNT];

static void
slab_link(struct slab *slab)
{
	size_t index = slab->class_index;
	slab->previous = NULL;
	slab->next = available[index];
	if (slab->next != NULL)
		slab->next->previous = slab;
	available[index] = slab;
}

static void
slab_unlink(struct slab *slab)
{
	if (slab->previous != NULL)
		slab->previous->next = slab->next;
	else
		available[slab->class_index] = slab->next;
	if (slab->next != NULL)
		slab->next->previous = slab->previous;
	slab->previous = slab->next = NULL;
}

/* Free-list links live in the payload, never in an allocated block. */
static struct block **
free_link(struct block *block)
{
	return (struct block **)(block + 1);
}

void *
malloc(size_t size)
{
	size_t stride = 32, index = 0;
	struct block *block;
	if (size == 0)
		size = 1;
	while (index < CLASS_COUNT && size > stride - sizeof(*block)) {
		index++;
		stride *= 2;
	}
	if (index < CLASS_COUNT) {
		struct slab *slab = available[index];
		if (slab == NULL) {
			slab = __libos_mmap(PAGE);
			if (slab == NULL)
				return NULL;
			*slab = (struct slab){ .magic = SMALL_MAGIC,
			    .class_index = index };
			for (size_t offset = sizeof(*slab); offset <= PAGE - stride;
			    offset += stride) {
				block = (struct block *)((uint8_t *)slab + offset);
				*block = (struct block){0};
				*free_link(block) = slab->free;
				slab->free = block;
			}
			slab_link(slab);
		}
		if (spare[index] == slab)
			spare[index] = NULL;
		block = slab->free;
		slab->free = *free_link(block);
		slab->live++;
		if (slab->free == NULL)
			slab_unlink(slab);
	} else {
		struct large *large;
		size_t pages;
		if (size > SIZE_MAX - sizeof(*large) - (PAGE - 1)) {
			errno = ENOMEM;
			return NULL;
		}
		pages = (size + sizeof(*large) + PAGE - 1) / PAGE;
		large = __libos_mmap(pages * PAGE);
		if (large == NULL)
			return NULL;
		large->magic = LARGE_MAGIC;
		large->pages = pages;
		block = &large->block;
	}
	*block = (struct block){ .magic = BLOCK_MAGIC, .size = size };
	return block + 1;
}

void
free(void *pointer)
{
	struct block *block;
	struct slab *slab;
	if (pointer == NULL)
		return;
	block = (struct block *)pointer - 1;
	if (block->magic != BLOCK_MAGIC)
		return;
	slab = (struct slab *)((uintptr_t)block & ~(uintptr_t)(PAGE - 1));
	if (slab->magic == LARGE_MAGIC) {
		struct large *large = (struct large *)slab;
		block->magic = 0;
		__libos_free_pages(large, large->pages * PAGE);
	} else if (slab->magic == SMALL_MAGIC) {
		size_t index = slab->class_index;
		block->magic = 0;
		if (slab->free == NULL)
			slab_link(slab);
		*free_link(block) = slab->free;
		slab->free = block;
		if (--slab->live == 0) {
			/* Retain one empty page per class for short-lived allocations;
			 * release every excess page after a burst. */
			if (spare[index] == NULL) {
				spare[index] = slab;
			} else {
				slab_unlink(slab);
				__libos_free_pages(slab, PAGE);
			}
		}
	}
}

void *
calloc(size_t count, size_t size)
{
	void *pointer;
	if (size != 0 && count > SIZE_MAX / size) {
		errno = ENOMEM;
		return NULL;
	}
	size *= count;
	pointer = malloc(size);
	if (pointer != NULL)
		memset(pointer, 0, size);
	return pointer;
}

void *
realloc(void *pointer, size_t size)
{
	struct block *block;
	struct slab *slab;
	size_t capacity;
	void *replacement;
	if (pointer == NULL)
		return malloc(size);
	if (size == 0) {
		free(pointer);
		return NULL;
	}
	block = (struct block *)pointer - 1;
	if (block->magic != BLOCK_MAGIC)
		return NULL;
	slab = (struct slab *)((uintptr_t)block & ~(uintptr_t)(PAGE - 1));
	if (slab->magic == SMALL_MAGIC)
		capacity = (32UL << slab->class_index) - sizeof(*block);
	else if (slab->magic == LARGE_MAGIC)
		capacity = ((struct large *)slab)->pages * PAGE - sizeof(struct large);
	else
		return NULL;
	if (size <= capacity) {
		block->size = size;
		return pointer;
	}
	replacement = malloc(size);
	if (replacement == NULL)
		return NULL;
	memcpy(replacement, pointer, block->size);
	free(pointer);
	return replacement;
}
