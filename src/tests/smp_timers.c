/* AP timer waits and interrupted nanosleep contexts. */
#include <stdbool.h>
#include "../user/include/os.h"
#include "abi/syscall.h"
#include "abi/process_info.h"

struct timespec { int64_t seconds, nanoseconds; };
static volatile unsigned handled;

static long call(uint64_t number, uintptr_t a, uintptr_t b, uintptr_t c)
{
	unsigned char carry;
	__asm__ volatile ("int $128; setc %1" : "+a" (number), "=qm" (carry) :
	    "D" (a), "S" (b), "d" (c) : "rcx", "r11", "memory");
	return carry ? -(long)number : (long)number;
}

static uint64_t now(void)
{
	struct timespec time;
	if (call(SYSCALL_CLOCK_GETTIME, 1, (uintptr_t)&time, 0) != 0)
		u_exit(10);
	return (uint64_t)time.seconds * 1000000000ULL + time.nanoseconds;
}

static long nanosleep(struct timespec *request, struct timespec *remaining)
{
	uint64_t token[2] = {0x736c656570736d70ULL, (uint64_t)u_getpid()};
	uint64_t observed[2];
	uint16_t control = (uint16_t)(0x037fU | ((token[1] & 3U) << 10));
	uint16_t observed_control;
	uint64_t number = SYSCALL_NANOSLEEP;
	unsigned char carry;
	/* Other sleeping processes use different XMM15 contents. */
	__asm__ volatile ("movdqu %4, %%xmm15; fldcw %5; int $128; setc %1; "
	    "movdqu %%xmm15, %2; fnstcw %3" :
	    "+a" (number), "=qm" (carry), "=m" (observed), "=m" (observed_control) :
	    "m" (token), "m" (control), "D" ((uintptr_t)request), "S" ((uintptr_t)remaining) :
	    "rcx", "r11", "xmm15", "memory");
	if (observed[0] != token[0] || observed[1] != token[1] || observed_control != control)
		u_exit(17);
	return carry ? -(long)number : (long)number;
}

static void handler(uint64_t signo)
{
	/* A handler may use SSE freely; sigreturn must restore interrupted data. */
	__asm__ volatile ("pxor %%xmm15, %%xmm15; fninit" : : : "xmm15");
	if (signo == 10)
		handled++;
}

static int timed_loops(unsigned id)
{
	struct timespec request = {0, 0}, remaining = {-1, -1};
	if (u_sleep_ticks(0) != 0 || nanosleep(&request, &remaining) != 0 ||
	    nanosleep((void *)1, NULL) != -14 || nanosleep(&request, (void *)1) != -14 ||
	    u_sleep_ticks(UINT64_MAX) != -22)
		return 11;
	request.seconds = -1;
	if (nanosleep(&request, NULL) != -22)
		return 12;
	request = (struct timespec){0, 1000000000};
	if (nanosleep(&request, NULL) != -22)
		return 13;
	request = (struct timespec){INT64_MAX, 0};
	if (nanosleep(&request, NULL) != -22)
		return 14;
	for (unsigned round = 0; round < 24; round++) {
		uint64_t start = now();
		if (u_sleep_ticks(1) != 0 || now() - start < 10000000)
			return 15;
		request = (struct timespec){0, (round % 2 == 0 ? 1 : 15000000) + id};
		start = now();
		if (nanosleep(&request, NULL) != 0 || now() - start < (uint64_t)request.nanoseconds)
			return 16;
	}
	return 0;
}

static int wait_blocked(long child)
{
	struct u_process_info entries[U_PROCESS_SNAPSHOT_MAX];
	for (unsigned attempt = 0; attempt < 2000; attempt++) {
		long count = u_process_snapshot(entries, U_PROCESS_SNAPSHOT_MAX);
		if (count < 0)
			return 1;
		for (long i = 0; i < count; i++)
			if (entries[i].pid == (uint64_t)child && entries[i].state == OS_PROCESS_BLOCKED)
				return 0;
		if (u_sleep_ticks(1) != 0)
			return 1;
	}
	return 1;
}

static int raced_signals(void)
{
	long base = u_shm_create(4096);
	if (base < 0)
		return 30;
	uint64_t *shared = (void *)base;
	long children[7];
	for (unsigned i = 0; i < 7; i++) {
		children[i] = u_fork();
		if (children[i] < 0)
			return 31;
		if (children[i] != 0)
			continue;
		struct u_sigaction action = {(uintptr_t)handler, (uintptr_t)u_sigreturn};
		if (u_sigaction(10, &action, NULL) != 0)
			u_exit(32);
		__atomic_store_n(&shared[i], 1, __ATOMIC_RELEASE);
		while (__atomic_load_n(&shared[7], __ATOMIC_ACQUIRE) == 0)
			__asm__ volatile ("pause");
		for (unsigned round = 0; round < 64; round++) {
			struct timespec request = {0, 1};
			long result = nanosleep(&request, NULL);
			if (result != 0 && result != -4)
				u_exit(33);
			uint64_t expected = round;
			if (!__atomic_compare_exchange_n(&shared[8 + i], &expected, round + 1,
			    false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
				u_exit(34);
		}
		if (handled == 0)
			u_exit(35);
		u_exit(0);
	}
	for (unsigned i = 0; i < 7; i++)
		for (unsigned attempt = 0; __atomic_load_n(&shared[i], __ATOMIC_ACQUIRE) == 0; attempt++)
			if (attempt == 2000 || u_sleep_ticks(1) != 0)
				return 36;
	__atomic_store_n(&shared[7], 1, __ATOMIC_RELEASE);
	for (unsigned round = 0; round < 1000; round++)
		for (unsigned i = 0; i < 7; i++) {
			long result = u_kill(children[i], 10);
			if (result != 0 && result != -3)
				return 37;
		}
	for (unsigned i = 0; i < 7; i++) {
		uint8_t status = 255;
		if (u_waitpid(children[i], &status) != children[i] || status != 0 ||
		    __atomic_load_n(&shared[8 + i], __ATOMIC_RELAXED) != 64) {
			u_puts("raced timer status ");
			u_put_u64(status);
			u_puts("\n");
			return 38;
		}
	}
	return u_shm_detach(base) == 0 ? 0 : 39;
}

int user_main(void)
{
	long children[7];
	for (unsigned i = 0; i < 7; i++) {
		children[i] = u_fork();
		if (children[i] < 0)
			return 1;
		if (children[i] == 0)
			u_exit(timed_loops(i));
	}
	for (unsigned i = 0; i < 7; i++) {
		uint8_t status = 255;
		if (u_waitpid(children[i], &status) != children[i] || status != 0) {
			u_puts("timer child status ");
			u_put_u64(status);
			u_puts("\n");
			return 2;
		}
	}
	/* Include NULL remainder, in-place request/remainder, and fatal interruption. */
	for (unsigned scenario = 0; scenario < 6; scenario++) {
		long child = u_fork();
		if (child < 0)
			return 3;
		if (child == 0) {
			struct u_sigaction action = {(uintptr_t)handler, (uintptr_t)u_sigreturn};
			if (u_sigaction(10, &action, NULL) != 0)
				u_exit(20);
			struct timespec request = {10, 0}, remaining = {-1, -1};
			struct timespec *left = scenario == 1 ? NULL : scenario == 3 ? &request : &remaining;
			uint64_t start = now();
			long result = scenario == 0 || scenario == 4 ? u_sleep_ticks(1000) : nanosleep(&request, left);
			uint64_t elapsed = now() - start;
			if (scenario >= 4 || result != -4 || handled != 1)
				u_exit(21);
			if (scenario != 0 && left != NULL) {
				if (left->seconds < 0 || left->nanoseconds < 0 || left->nanoseconds >= 1000000000)
					u_exit(22);
				uint64_t remainder = (uint64_t)left->seconds * 1000000000ULL + left->nanoseconds;
				if (remainder == 0 || remainder > 10000000000ULL ||
				    remainder + elapsed < 10000000000ULL)
					u_exit(23);
			}
			request = (struct timespec){0, 1};
			if (nanosleep(&request, NULL) != 0 || handled != 1)
				u_exit(24);
			u_exit(0);
		}
		if (wait_blocked(child) != 0 || u_sleep_ticks(3) != 0 ||
		    u_kill(child, scenario >= 4 ? U_SIGKILL : 10) != 0)
			return 4;
		uint8_t status = 255;
		if (u_waitpid(child, &status) != child || status != (scenario >= 4 ? 137 : 0)) {
			u_puts("interrupted timer status ");
			u_put_u64(status);
			u_puts("\n");
			return 5;
		}
	}
	if (raced_signals() != 0)
		return 6;
	u_puts("SMP timers fixture passed\n");
	return 0;
}
