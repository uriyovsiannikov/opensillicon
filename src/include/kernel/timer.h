#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdbool.h>
#include <stdint.h>

/* Scheduler tick source and conversions share one frequency. */
#define TIMER_TICK_HZ 100U
#define TIMER_TICK_NS (1000000000ULL / TIMER_TICK_HZ)
_Static_assert(1000000000ULL % TIMER_TICK_HZ == 0, "timer ticks must divide a second");

typedef void (*kernel_timer_callback)(void *argument);

bool timer_init(uint64_t tsc_frequency_hz);
uint64_t timer_ticks(void);
void timer_set_realtime_epoch(uint64_t seconds);
bool timer_realtime_epoch(uint64_t *seconds);
/* Per-CPU scheduler tick count; AP counts use the AP idle LAPIC timer. */
uint64_t timer_jiffies(void);
uint64_t timer_monotonic_ns(void);
uint32_t timer_ap_initial_count(void);
/* Wait only outside locks/guards, after timer_init. Temporarily enables
 * interrupts and restores the caller's interrupt-enable state on return. */
void timer_wait_for_interrupt(void);
bool timer_callback_schedule(uint64_t deadline_ns,
	kernel_timer_callback callback, void *argument, uint64_t *handle);
bool timer_callback_cancel(uint64_t handle);

#endif
