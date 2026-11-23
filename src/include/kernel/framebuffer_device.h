#ifndef KERNEL_FRAMEBUFFER_DEVICE_H
#define KERNEL_FRAMEBUFFER_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "abi/framebuffer.h"
#include "limine.h"

bool framebuffer_device_init(const struct limine_framebuffer *framebuffer);
bool framebuffer_device_info(struct os_framebuffer_info *info);
/* Kernel-only boot metadata. Limine framebuffer.address uses the HHDM;
 * this is a CPU physical aperture address, not a GPU VRAM address. */
bool framebuffer_device_boot_physical(uint64_t *base, uint64_t *size);
/* Read-only kernel sampling of the existing boot aperture, in DWORDs. */
bool framebuffer_device_boot_read(uint64_t offset, uint32_t *words, size_t count);
int framebuffer_device_present_rect(const struct os_framebuffer_rect *rect);

#endif
