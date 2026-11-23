#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "kernel/hda_audio.h"
#include "arch/x86_64/lapic.h"
#include "kernel/process.h"
#include "kernel/smp.h"
#include "kernel/timer.h"

#define TIMER_VECTOR 32U
#define PIC_MASTER_DATA 0x21U
#define PIC_SLAVE_DATA 0xa1U

static uint64_t ticks;
static uint64_t tsc_frequency_hz;
static uint32_t ap_initial_count;
static bool initialized;
static uint64_t realtime_epoch;
static bool realtime_available;

static struct interrupt_frame *
timer_interrupt(struct interrupt_frame *frame)
{
	uint64_t current = __atomic_load_n(&ticks, __ATOMIC_RELAXED);

	if (current != UINT64_MAX)
		current = __atomic_add_fetch(&ticks, 1, __ATOMIC_RELAXED);
	lapic_eoi();
	hda_audio_tick();
	process_timer_wakeup(current);
	return process_timer_preempt(frame);
}

bool
timer_init(uint64_t tsc_hz)
{
	uint32_t initial_count;
	uint64_t flags;
	bool success = false;

	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE))
		goto out;
	if (tsc_hz > UINT64_MAX / UINT64_C(1000000000))
		tsc_hz = 0;
	__atomic_store_n(&tsc_frequency_hz, tsc_hz, __ATOMIC_RELAXED);
	__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)0xff),
	    "Nd" ((uint16_t)PIC_MASTER_DATA));
	__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)0xff),
	    "Nd" ((uint16_t)PIC_SLAVE_DATA));
	if (!lapic_timer_calibrate(TIMER_TICK_HZ, &initial_count) ||
	    !interrupt_register_handler(TIMER_VECTOR, timer_interrupt))
		goto out;
	ap_initial_count = initial_count;
	__atomic_store_n(&ticks, 0, __ATOMIC_RELAXED);
	if (!lapic_timer_start(TIMER_VECTOR, initial_count))
		goto out;
	__atomic_store_n(&initialized, true, __ATOMIC_RELEASE);
	success = true;
out:
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return success;
}

uint64_t
timer_ticks(void)
{
	return __atomic_load_n(&initialized, __ATOMIC_ACQUIRE) ?
	    __atomic_load_n(&ticks, __ATOMIC_RELAXED) : 0;
}

void
timer_set_realtime_epoch(uint64_t seconds)
{
	__atomic_store_n(&realtime_epoch, seconds, __ATOMIC_RELAXED);
	__atomic_store_n(&realtime_available, true, __ATOMIC_RELEASE);
}

bool
timer_realtime_epoch(uint64_t *seconds)
{
	if (!__atomic_load_n(&realtime_available, __ATOMIC_ACQUIRE))
		return false;
	*seconds = __atomic_load_n(&realtime_epoch, __ATOMIC_RELAXED);
	return true;
}

uint64_t
timer_jiffies(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	if (cpu != NULL && !cpu->bsp)
		return __atomic_load_n(&cpu->idle_timer_ticks, __ATOMIC_ACQUIRE);
	return timer_ticks();
}

uint64_t
timer_monotonic_ns(void)
{
	uint64_t frequency = __atomic_load_n(&tsc_frequency_hz, __ATOMIC_ACQUIRE);
	uint64_t cycles;
	uint64_t tick_count;
	uint32_t low, high;

	if (frequency == 0) {
		tick_count = timer_ticks();
		return tick_count > UINT64_MAX / TIMER_TICK_NS ? UINT64_MAX :
		    tick_count * TIMER_TICK_NS;
	}
	__asm__ volatile ("lfence; rdtsc" : "=a" (low), "=d" (high) : : "memory");
	cycles = ((uint64_t)high << 32) | low;
	return (cycles / frequency) * UINT64_C(1000000000) +
	    ((cycles % frequency) * UINT64_C(1000000000)) / frequency;
}

uint32_t
timer_ap_initial_count(void)
{
	return __atomic_load_n(&initialized, __ATOMIC_ACQUIRE) ?
	    ap_initial_count : 0;
}

void
timer_wait_for_interrupt(void)
{
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE))
		__asm__ volatile ("sti; hlt; cli" : : : "memory");
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
}
