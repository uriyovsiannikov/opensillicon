#define _DEFAULT_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

void *os_malloc(size_t);
void *os_calloc(size_t, size_t);
void *os_realloc(void *, size_t);
void os_free(void *);

static size_t maps, pages, peak;
static int fail_mapping;

void *
__libos_mmap(size_t size)
{
	if (fail_mapping) {
		errno = ENOMEM;
		return NULL;
	}
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(p != MAP_FAILED);
	maps++;
	pages += size / 4096;
	if (pages > peak)
		peak = pages;
	return p;
}

void
__libos_free_pages(void *pointer, size_t size)
{
	assert(munmap(pointer, size) == 0);
	assert(pages >= size / 4096);
	pages -= size / 4096;
}

static uint32_t random_state = 19;
static unsigned
random_number(void)
{
	random_state = random_state * 1664525U + 1013904223U;
	return random_state;
}

int
main(void)
{
	void *small[10000];
	for (size_t i = 0; i < 10000; i++) {
		small[i] = os_malloc(24);
		assert(small[i] != NULL && (uintptr_t)small[i] % 16 == 0);
		memset(small[i], (int)(i & 255), 24);
	}
	assert(maps < 170); /* Former allocator needed 10,000 mappings. */
	for (size_t i = 0; i < 10000; i++) {
		for (size_t j = 0; j < 24; j++)
			assert(((unsigned char *)small[i])[j] == (i & 255));
		os_free(small[i]);
	}
	assert(pages == 1);
	size_t before = maps;
	for (unsigned i = 0; i < 10000; i++)
		os_free(os_malloc(24));
	assert(maps == before);

	struct slot { unsigned char *p; size_t size; unsigned char value; } slots[512] = {0};
	for (unsigned iteration = 0; iteration < 100000; iteration++) {
		struct slot *slot = &slots[random_number() % 512];
		for (size_t j = 0; j < slot->size; j++)
			assert(slot->p[j] == slot->value);
		size_t size = random_number() % 10000 + 1;
		if ((random_number() % 4) == 0) {
			os_free(slot->p);
			*slot = (struct slot){0};
			continue;
		}
		unsigned char *p = os_realloc(slot->p, size);
		assert(p != NULL && (uintptr_t)p % 16 == 0);
		for (size_t j = 0; j < size && j < slot->size; j++)
			assert(p[j] == slot->value);
		slot->value = (unsigned char)iteration;
		memset(p, slot->value, size);
		slot->p = p;
		slot->size = size;
	}
	for (size_t i = 0; i < 512; i++)
		os_free(slots[i].p);
	assert(pages <= 7);

	for (size_t size = 1; size < 10000; size += 31) {
		unsigned char *p = os_calloc(size, 1);
		assert(p != NULL);
		for (size_t i = 0; i < size; i++)
			assert(p[i] == 0);
		os_free(p);
	}
	void *p = os_malloc(33);
	assert(os_realloc(p, 47) == p);
	os_free(p);
	p = os_malloc(3000);
	assert(os_realloc(p, 4000) == p);
	memset(p, 0x42, 4000);
	fail_mapping = 1;
	assert(os_realloc(p, 16000) == NULL);
	for (size_t i = 0; i < 4000; i++)
		assert(((unsigned char *)p)[i] == 0x42);
	assert(os_malloc(SIZE_MAX) == NULL && errno == ENOMEM);
	assert(os_calloc(SIZE_MAX / 2, 3) == NULL && errno == ENOMEM);
	assert(os_realloc(p, 0) == NULL);
	os_free(os_malloc(0));
	os_free(NULL);
	printf("allocator PASS: 10000 small allocations used %zu maps; stress peak %zu pages\n",
	    before, peak);
}
