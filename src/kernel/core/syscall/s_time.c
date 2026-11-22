#include "syscall_internal.h"
#include "abi/time.h"

#include "kernel/process.h"
#include "kernel/timer.h"
#include "kernel/vmm.h"

bool
syscall_time_local(struct interrupt_frame **result_frame)
{
	struct interrupt_frame *frame = *result_frame;
	uint64_t error;

	switch (frame->rax) {
	case SYSCALL_NANOSLEEP:
		{
			struct os_timespec request;
			struct address_space *space = vmm_active_address_space();
			uint64_t ticks;

			if (!vmm_copy_from_user(space, &request, frame->rdi,
			    sizeof(request))) {
				syscall_error(frame, EFAULT);
				return true;
			}
			if (request.tv_sec < 0 || request.tv_nsec < 0 ||
			    request.tv_nsec >= 1000000000LL) {
				syscall_error(frame, EINVAL);
				return true;
			}
			if (frame->rsi != 0 && !vmm_validate_user_write(space,
			    frame->rsi, sizeof(request))) {
				syscall_error(frame, EFAULT);
				return true;
			}
			if ((uint64_t)request.tv_sec >
			    (UINT64_MAX - 1U) / TIMER_TICK_HZ) {
				syscall_error(frame, EINVAL);
				return true;
			}
			ticks = (uint64_t)request.tv_sec * TIMER_TICK_HZ;
			if (request.tv_nsec != 0) {
				uint64_t fractional_ticks =
				    ((uint64_t)request.tv_nsec + TIMER_TICK_NS - 1U) /
				    TIMER_TICK_NS;
				if (ticks > UINT64_MAX - fractional_ticks - 1U) {
					syscall_error(frame, EINVAL);
					return true;
				}
				ticks += fractional_ticks;
			}
			if (ticks != 0) {
				if (ticks == UINT64_MAX) {
					syscall_error(frame, EINVAL);
					return true;
				}
				/* One extra tick accounts for unknown phase within the
				 * current tick; the wait therefore cannot finish early. */
				ticks++;
			}
			*result_frame = process_nanosleep(frame, ticks, request.tv_sec,
			    request.tv_nsec, frame->rsi, &error);
		}
		if (error != 0)
			syscall_error(*result_frame, error);
		return true;
	case SYSCALL_CLOCK_GETTIME:
		{
			struct os_timespec value;
			struct address_space *space = vmm_active_address_space();
			uint64_t ticks;

			if (frame->rdi != OS_CLOCK_MONOTONIC && frame->rdi != OS_CLOCK_REALTIME) {
				syscall_error(frame, EINVAL);
				return true;
			}
			if (!vmm_validate_user_write(space, frame->rsi,
			    sizeof(value))) {
				syscall_error(frame, EFAULT);
				return true;
			}
			ticks = timer_ticks();
			value.tv_sec = (int64_t)(ticks / TIMER_TICK_HZ);
			if (frame->rdi == OS_CLOCK_REALTIME) {
				uint64_t epoch;
				if (!timer_realtime_epoch(&epoch) ||
				    epoch > INT64_MAX - (uint64_t)value.tv_sec) {
					syscall_error(frame, EIO);
					return true;
				}
				value.tv_sec += (int64_t)epoch;
			}
			value.tv_nsec = (int64_t)(ticks % TIMER_TICK_HZ) *
			    TIMER_TICK_NS;
			if (!vmm_copy_to_user(space, frame->rsi, &value,
			    sizeof(value))) {
				syscall_error(frame, EFAULT);
				return true;
			}
			frame->rax = 0;
		}
		return true;
	default:
		return false;
	}
}
