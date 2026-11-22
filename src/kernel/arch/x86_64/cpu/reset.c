#include <stdint.h>

#include "arch/x86_64/acpi.h"
#include "arch/x86_64/reset.h"

void
arch_reboot(void)
{
	acpi_reboot();
	/* PC reset control remains available when firmware has no usable reset. */
	uint8_t reset = 0x02;
	__asm__ volatile ("outb %0, %1" : : "a" (reset),
	    "Nd" ((uint16_t)0x0cf9));
	for (volatile unsigned int delay = 0; delay < 100000; delay++)
		__asm__ volatile ("pause");
	reset = 0x06;
	__asm__ volatile ("outb %0, %1" : : "a" (reset),
	    "Nd" ((uint16_t)0x0cf9));
}
