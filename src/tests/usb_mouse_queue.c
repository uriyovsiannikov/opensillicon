#include <assert.h>
#include <stdio.h>

#include "../kernel/drivers/input/usb/opensillicon/host.c"

static uint64_t test_clock;

uint64_t
timer_monotonic_ns(void)
{
	return test_clock;
}

static void
check_keyboard_repeat(void)
{
	struct terminal_key_event event;

	keyboard_event(&keyboards[0], 0x2a, true); /* Backspace */
	assert(key_count == 1);
	test_clock = 499000000;
	assert(!usb_host_keyboard_repeat_poll(&event));
	test_clock = 500000000;
	assert(usb_host_keyboard_repeat_poll(&event));
	assert(event.pressed && !event.extended && event.scancode == 0x0e);
	assert(!usb_host_keyboard_repeat_poll(&event));
	test_clock += 33000000;
	assert(usb_host_keyboard_repeat_poll(&event));
	assert(key_count == 1); /* Raw USB events do not contain repeats. */

	keyboard_event(&keyboards[0], 0x2a, false);
	test_clock += 1000000000;
	assert(!usb_host_keyboard_repeat_poll(&event));
	assert(key_count == 2 && !key_queue[1].pressed);

	keyboard_event(&keyboards[0], 0x39, true); /* Caps Lock */
	test_clock += 1000000000;
	assert(!usb_host_keyboard_repeat_poll(&event));
	puts("USB keyboard repeat delay, rate and release: PASS");
}

int
main(void)
{
	check_keyboard_repeat();

	/* Motion produced faster than X can consume it must not build a backlog. */
	for (unsigned int i = 0; i < 1000; i++)
		mouse_event(1, -1);
	assert(mouse_count == 1);
	assert(mouse_queue[mouse_head].dx == 1000);
	assert(mouse_queue[mouse_head].dy == -1000);

	/* Preserve button boundaries while coalescing motion within each state. */
	mice[0].buttons = 1;
	mouse_event(2, 3);
	mouse_event(4, 5);
	mice[0].buttons = 0;
	mouse_event(7, 8);
	assert(mouse_count == 3);
	assert(mouse_queue[1].buttons == 1 && mouse_queue[1].dx == 6 &&
	    mouse_queue[1].dy == 8);
	assert(mouse_queue[2].buttons == 0 && mouse_queue[2].dx == 7);

	/* Split a movement sum that cannot fit in one ABI event. */
	mouse_head = mouse_count = 0;
	mouse_event(INT16_MAX, INT16_MIN);
	mouse_event(1, -1);
	assert(mouse_count == 2);
	assert(mouse_queue[0].dx == INT16_MAX && mouse_queue[0].dy == INT16_MIN);
	assert(mouse_queue[1].dx == 1 && mouse_queue[1].dy == -1);
	puts("USB mouse motion batching and button boundaries: PASS");
}
