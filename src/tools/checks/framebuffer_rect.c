#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "kernel/framebuffer_device.h"
#include "kernel/process.h"
#include "kernel/tty.h"
#include "kernel/vmm.h"
#include "kernel/errno.h"

static struct address_space space;
static unsigned char source[4 * 5200];
static unsigned char pixels[4 * 5200];
static uint32_t uid;

bool process_current_credentials(struct os_credentials *credentials)
{
	*credentials = (struct os_credentials){.effective_uid = uid};
	return true;
}
uint64_t process_current_pid(void) { return 7; }
bool tty_console_graphics_owned(uint64_t owner) { (void)owner; return false; }
struct address_space *vmm_active_address_space(void) { return &space; }
bool vmm_validate_user_read(const struct address_space *mapping, uint64_t address, size_t size)
{
	uint64_t start = (uintptr_t)source;
	return mapping == &space && address >= start && address - start <= sizeof(source) &&
	    size <= sizeof(source) - (address - start);
}
bool vmm_copy_from_user(const struct address_space *mapping, void *target, uint64_t address, size_t size)
{
	if (!vmm_validate_user_read(mapping, address, size)) return false;
	memcpy(target, (void *)(uintptr_t)address, size);
	return true;
}

int main(void)
{
	struct limine_framebuffer fb = {.address = pixels, .width = 1300,
	    .height = 4, .pitch = 5200, .bpp = 32};
	assert(framebuffer_device_init(&fb));
	for (size_t i = 0; i < sizeof(source); i++) source[i] = (unsigned char)(i * 13 + 9);
	memset(pixels, 0xa5, sizeof(pixels));
	struct os_framebuffer_rect rect = {.source = (uintptr_t)source,
	    .x = 1, .y = 1, .width = 1280, .height = 2, .source_pitch = 5184};
	assert(framebuffer_device_present_rect(&rect) == 0);
	for (size_t y = 0; y < 4; y++) for (size_t x = 0; x < 5200; x++) {
		unsigned char expected = y >= 1 && y <= 2 && x >= 4 && x < 5124 ?
		    source[(y - 1) * 5184 + x - 4] : 0xa5;
		assert(pixels[y * 5200 + x] == expected);
	}
	unsigned char saved[sizeof(pixels)]; memcpy(saved, pixels, sizeof(saved));
	rect.source_pitch = 0;
	assert(framebuffer_device_present_rect(&rect) == -EINVAL);
	rect.source_pitch = 5119;
	assert(framebuffer_device_present_rect(&rect) == -EINVAL);
	rect.source_pitch = UINT64_MAX;
	assert(framebuffer_device_present_rect(&rect) == -EINVAL);
	rect.source_pitch = 5184; rect.source = UINT64_MAX - 1;
	assert(framebuffer_device_present_rect(&rect) == -EINVAL);
	rect.source = 1;
	assert(framebuffer_device_present_rect(&rect) == -EFAULT);
	rect.source = (uintptr_t)source; rect.x = 1300;
	assert(framebuffer_device_present_rect(&rect) == -EINVAL);
	assert(memcmp(saved, pixels, sizeof(saved)) == 0);
	uid = 1000; rect.x = 1;
	assert(framebuffer_device_present_rect(&rect) == -EPERM);
	uid = 0;
	rect = (struct os_framebuffer_rect){.source = (uintptr_t)source,
	    .width = 1300, .height = 4, .source_pitch = 5200};
	assert(framebuffer_device_present_rect(&rect) == 0);
	assert(memcmp(source, pixels, sizeof(pixels)) == 0);
	puts("FRAMEBUFFER_RECT_PASS: explicit stride, chunked rows, clipping, overflow and permissions");
	return 0;
}
