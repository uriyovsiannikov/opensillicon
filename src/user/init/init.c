#include <stdio.h>

#include "include/os.h"
#include "common/logo_pixels.h"
#include "service.h"

static uint32_t init_logo_pixels[OS_LOGO_WIDTH * OS_LOGO_HEIGHT];

static size_t
cpu_brand(char *buffer, size_t capacity)
{
	uint32_t eax, ebx, ecx, edx;
	uint32_t words[12];
	size_t length = 0;
	__asm__ volatile ("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx),
	    "=d" (edx) : "a" (0x80000000U), "c" (0));
	if (eax < 0x80000004U || capacity < sizeof(words))
		return 0;
	for (uint32_t leaf = 0; leaf < 3; leaf++) {
		uint32_t query = 0x80000002U + leaf;
		__asm__ volatile ("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx),
		    "=d" (edx) : "a" (query), "c" (0));
		words[leaf * 4U] = eax;
		words[leaf * 4U + 1U] = ebx;
		words[leaf * 4U + 2U] = ecx;
		words[leaf * 4U + 3U] = edx;
	}
	for (size_t i = 0; i < sizeof(words); i++) {
		char character = (char)(words[i / 4U] >> ((i % 4U) * 8U));
		if (character == '\0')
			break;
		buffer[length++] = character;
	}
	while (length != 0 && buffer[length - 1U] == ' ')
		length--;
	return length;
}

static uint32_t
logo_channel(uint8_t value, uint8_t size, uint8_t shift)
{
	uint32_t maximum = (1U << size) - 1U;
	return ((uint32_t)value * maximum + 127U) / 255U << shift;
}

static void
show_init_banner(void)
{
	struct os_framebuffer_info info;
	struct os_framebuffer_rect rect;
	char processor[48];
	size_t processor_length = cpu_brand(processor, sizeof(processor));
	if (u_framebuffer_info(&info) == 0 && info.bits_per_pixel == 32 &&
	    info.red_mask_size != 0 && info.red_mask_size <= 8 &&
	    info.green_mask_size != 0 && info.green_mask_size <= 8 &&
	    info.blue_mask_size != 0 && info.blue_mask_size <= 8 &&
	    OS_LOGO_WIDTH + 12U <= info.width &&
	    OS_LOGO_HEIGHT + 12U <= info.height) {
		for (size_t i = 0; i < OS_LOGO_WIDTH * OS_LOGO_HEIGHT; i++) {
			uint8_t packed = os_logo_gray4[i / 2U];
			uint8_t gray = (uint8_t)(((i & 1U) == 0 ? packed >> 4 :
			    packed & 0x0fU) * 17U);
			init_logo_pixels[i] = logo_channel(gray, info.red_mask_size,
			    info.red_mask_shift) |
			    logo_channel(gray, info.green_mask_size,
			    info.green_mask_shift) |
			    logo_channel(gray, info.blue_mask_size,
			    info.blue_mask_shift);
		}
		rect = (struct os_framebuffer_rect){
			.source = (uintptr_t)init_logo_pixels,
			.x = 12,
			.y = 12,
			.width = OS_LOGO_WIDTH,
			.height = OS_LOGO_HEIGHT,
			.source_pitch = OS_LOGO_WIDTH * sizeof(uint32_t)
		};
		(void)u_framebuffer_present_rect(&rect);
	}
	fputs("\033[3;16H\033[36mWelcome! Running on ", stdout);
	if (processor_length != 0)
		fwrite(processor, 1, processor_length, stdout);
	else
		fputs("unknown CPU", stdout);
	fputs("\033[0m\033[6;1H", stdout);
}

/* musl CRT initializes TLS, errno and constructors before entering init. */
int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	show_init_banner();
	puts("\033[34m*\033[0m Initializing init ...                       \033[32m[ OK ]\033[0m");
	service_manager();
}
