/* Kernel-only boot framebuffer metadata/sampling bounds. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdalign.h>
#include "kernel/framebuffer_device.h"
static uint64_t hhdm;
uint64_t vmm_hhdm_offset(void){return hhdm;}
int main(void)
{
	alignas(4) uint32_t pixels[32],words[16];
	for(unsigned i=0;i<32;i++)pixels[i]=i^0xa5a5;
	uint64_t base,size;
	assert(!framebuffer_device_boot_physical(&base,&size));
	assert(!framebuffer_device_boot_read(0,words,1));
	struct limine_framebuffer fb={.address=pixels,.width=8,.height=4,.pitch=32,.bpp=32};
	assert(framebuffer_device_init(&fb));
	hhdm=(uintptr_t)pixels-0x1000000;
	assert(framebuffer_device_boot_physical(&base,&size)&&base==0x1000000&&size==128);
	assert(framebuffer_device_boot_read(0,words,16));
	for(unsigned i=0;i<16;i++)assert(words[i]==pixels[i]);
	assert(framebuffer_device_boot_read(64,words,16));
	for(unsigned i=0;i<16;i++)assert(words[i]==pixels[16+i]);
	assert(!framebuffer_device_boot_read(68,words,16));
	assert(!framebuffer_device_boot_read(128,words,1));
	assert(!framebuffer_device_boot_read(UINT64_MAX,words,1));
	assert(!framebuffer_device_boot_read(1,words,1));
	assert(!framebuffer_device_boot_read(0,words,17));
	assert(!framebuffer_device_boot_read(0,words,0));
	assert(!framebuffer_device_boot_read(0,NULL,1));
	assert(!framebuffer_device_boot_physical(NULL,&size));
	hhdm=(uintptr_t)pixels+1;
	assert(!framebuffer_device_boot_physical(&base,&size)&&!base&&!size);
	fb.address=(uint8_t *)pixels+1;assert(framebuffer_device_init(&fb));
	assert(!framebuffer_device_boot_read(0,words,1));
	assert(!framebuffer_device_init(NULL));
	assert(!framebuffer_device_boot_read(0,words,1));
	for(unsigned i=0;i<32;i++)assert(pixels[i]==(i^0xa5a5));
	puts("NOUVEAU_FRAMEBUFFER_PASS: kernel boot metadata and readonly sample bounds/alignment");
}
