#include <stdbool.h>

#include "../user/include/os.h"

#ifdef USBX_TEST_KEYBOARD
#include "abi/syscall.h"

static int check_mouse(void)
{
	bool pressed = false;
	int dx = 0, dy = 0;
	for (unsigned int i = 0; i < 400; i++) {
		struct os_mouse_event event;
		long status;
		bool failed;
		__asm__ volatile ("int $0x80" : "=a" (status), "=@ccc" (failed)
		    : "0" ((long)SYSCALL_MOUSE_POLL), "D" (&event), "S" (0L), "d" (0L)
		    : "memory");
		if (failed)
			return 9;
		if (status > 0) {
			dx += event.dx;
			dy += event.dy;
			if (!pressed && (event.buttons & 1) != 0) {
				pressed = true;
				u_puts("USBX test mouse button pressed\n");
			}
			if (pressed && event.buttons == 0 && dx == 12 && dy == -7) {
				u_puts("USBX test mouse movement and buttons succeeded\n");
				return 0;
			}
		}
		if (u_sleep_ticks(1) != 0)
			break;
	}
	return 10;
}
#endif

int user_main(void)
{
	/* Allow the BSP poll loop to run the asynchronous USBX enumeration. */
	if (u_sleep_ticks(100) != 0)
		return 4;
	long fd = u_open("/tmp/btlog.log", 0);
	char buffer[1024];

	if (fd < 0)
		return 1;
	for (;;) {
		long amount = u_read(fd, buffer, sizeof(buffer));
		if (amount < 0)
			return 2;
		if (amount == 0)
			break;
		u_putn(buffer, (size_t)amount);
	}
	if (u_close(fd) != 0)
		return 3;
	u_puts("USBX test init ready\n");
#ifdef USBX_TEST_KEYBOARD
	/* Verify press and release, including the extended cursor-key code. */
	bool pressed = false;
	unsigned int pairs = 0;
	for (unsigned int i = 0; i < 400; i++) {
		struct os_keyboard_event event;
		long status = u_keyboard_poll(&event);
		if (status < 0)
			return 5;
		if (status > 0 && !event.extended && event.scancode == 0x30 && !event.pressed)
			pairs++;
		if (status > 0 && event.extended && event.scancode == 0x48) {
			if (event.pressed) {
				pressed = true;
			} else if (pressed) {
				if (pairs != 36) {
					u_puts("USBX test repeated key releases: ");
					u_put_u64(pairs);
					u_puts("\n");
					return 11;
				}
				u_puts("USBX test cursor key pressed and released\n");
				char line[8];
				long amount = u_read(0, line, sizeof(line));
				if (amount != 2 || line[0] != 'A' || line[1] != '\n')
					return 6;
				u_puts("USBX test console read succeeded\n");
				return check_mouse();
			}
		}
		if (u_sleep_ticks(1) != 0)
			return 7;
	}
	u_puts("USBX test timed out after repeated key releases: ");
	u_put_u64(pairs);
	u_puts("\n");
	return 8;
#else
	return u_sleep_ticks(300) == 0 ? 0 : 5;
#endif
}
