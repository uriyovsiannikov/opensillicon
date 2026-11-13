/* Masked delivery, handler return and invalid local signal calls across CPUs. */
#include "../user/include/os.h"
#include "abi/syscall.h"
#define TEST_SIGNAL 10U

static volatile unsigned delivered;
static volatile unsigned wrong_signal;
static volatile unsigned invalid_return;

static void handler(uint64_t signo)
{
	if (invalid_return != 0)
		u_sigreturn(); /* A function call is not a valid signal-restorer frame. */
	if (signo != TEST_SIGNAL)
		wrong_signal = 1;
	delivered++;
}

static long mask(int how, const uint64_t *set, uint64_t *old)
{
	uint64_t result = SYSCALL_SIGPROCMASK;
	unsigned char failed;
	__asm__ volatile ("int $128; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" ((uint64_t)how), "S" ((uintptr_t)set), "d" ((uintptr_t)old) :
	    "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

/* Publish readiness only after discarding the usable user stack. */
__attribute__((naked, noreturn)) static void bad_stack(uint64_t *ready __attribute__((unused)))
{
	__asm__ volatile ("mov $0x10000, %rsp; movq $1, (%rdi); 1: pause; jmp 1b");
}

static long wait_nohang(long child, uint8_t *status)
{
	uint64_t result = SYSCALL_WAITPID;
	unsigned char failed;
	__asm__ volatile ("int $128; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" ((uint64_t)child), "S" ((uintptr_t)status), "d" (1ULL) :
	    "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

int user_main(void)
{
	long base = u_shm_create(4096);
	if (base < 0)
		return 1;
	uint64_t *shared = (void *)base;
	long children[7];
	for (unsigned i = 0; i < 7; i++) {
		children[i] = u_fork();
		if (children[i] < 0)
			return 2;
		if (children[i] != 0)
			continue;
		struct u_sigaction action = {(uintptr_t)handler, (uintptr_t)u_sigreturn};
		struct u_sigaction previous;
		uint64_t bit = 1ULL << TEST_SIGNAL;
		uint64_t kill_bit = 1ULL << U_SIGKILL;
		uint64_t old;
		if (u_sigaction(TEST_SIGNAL, &action, &previous) != 0 ||
		    previous.handler != 0 ||
		    u_sigaction(TEST_SIGNAL, NULL, &previous) != 0 ||
		    previous.handler != action.handler || previous.restorer != action.restorer)
			u_exit(10);
		if (mask(3, &bit, NULL) != -22 || mask(-1, &bit, NULL) != -22 ||
		    mask(0, (void *)1, NULL) != -14 ||
		    mask(0, &bit, (void *)1) != -14 ||
		    u_sigaction(TEST_SIGNAL, (void *)1, NULL) != -14 ||
		    u_sigaction(U_SIGKILL, &action, NULL) != -22)
			u_exit(11);
		if (mask(0, &kill_bit, NULL) != 0 || mask(0, NULL, &old) != 0 ||
		    (old & kill_bit) != 0)
			u_exit(12);
		for (unsigned round = 0; round < 20; round++) {
			if (mask(0, &bit, &old) != 0 || (old & bit) != 0)
				u_exit(13);
			__atomic_store_n(&shared[i], 4 * round + 1, __ATOMIC_RELEASE);
			while (__atomic_load_n(&shared[8 + i], __ATOMIC_ACQUIRE) != 4 * round + 2)
				__asm__ volatile ("pause");
			if (delivered != 2 * round || wrong_signal != 0)
				u_exit(14);
			/* Pending signal must run before returning to userspace. */
			if (mask(1, &bit, &old) != 0 || (old & bit) == 0 ||
			    delivered != 2 * round + 1 || wrong_signal != 0)
				u_exit(15);
			/* This signal arrives through an IPI while the mask is clear. */
			__atomic_store_n(&shared[i], 4 * round + 3, __ATOMIC_RELEASE);
			while (delivered != 2 * round + 2)
				__asm__ volatile ("pause");
			while (__atomic_load_n(&shared[8 + i], __ATOMIC_ACQUIRE) != 4 * round + 4)
				__asm__ volatile ("pause");
			if (wrong_signal != 0)
				u_exit(17);
		}
		u_exit(0);
	}
	for (unsigned round = 0; round < 20; round++) {
		for (unsigned i = 0; i < 7; i++) {
			for (unsigned attempts = 0;
			    __atomic_load_n(&shared[i], __ATOMIC_ACQUIRE) != 4 * round + 1;
			    attempts++) {
				if (attempts == 2000 || u_sleep_ticks(1) != 0)
					return 3;
			}
			if (u_kill(children[i], TEST_SIGNAL) != 0)
				return 4;
			__atomic_store_n(&shared[8 + i], 4 * round + 2, __ATOMIC_RELEASE);
			for (unsigned attempts = 0;
			    __atomic_load_n(&shared[i], __ATOMIC_ACQUIRE) != 4 * round + 3;
			    attempts++) {
				if (attempts == 2000 || u_sleep_ticks(1) != 0)
					return 10;
			}
			if (u_kill(children[i], TEST_SIGNAL) != 0)
				return 11;
			__atomic_store_n(&shared[8 + i], 4 * round + 4, __ATOMIC_RELEASE);
		}
	}
	for (unsigned i = 0; i < 7; i++) {
		uint8_t status = 255;
		if (u_waitpid(children[i], &status) != children[i] || status != 0) {
			u_puts("signal child status ");
			u_put_u64(status);
			u_puts("\n");
			return 5;
		}
	}
	/* Invalid signal contexts must use BSP teardown without corrupting a CPU. */
	for (unsigned broken = 0; broken < 2; broken++) {
		__atomic_store_n(&shared[0], 0, __ATOMIC_RELEASE);
		long child = u_fork();
		if (child < 0)
			return 12;
		if (child == 0) {
			struct u_sigaction action = {(uintptr_t)handler, (uintptr_t)u_sigreturn};
			invalid_return = 1;
			if (u_sigaction(TEST_SIGNAL, &action, NULL) != 0)
				u_exit(18);
			if (broken != 0)
				bad_stack(&shared[0]);
			__atomic_store_n(&shared[0], 1, __ATOMIC_RELEASE);
			for (;;)
				__asm__ volatile ("pause");
		}
		for (unsigned attempts = 0;
		    __atomic_load_n(&shared[0], __ATOMIC_ACQUIRE) == 0; attempts++) {
			/* Stay runnable: an otherwise idle BSP may legitimately reclaim
			 * this sole child instead of leaving it to the AP. */
			if (attempts == 2000000 || u_getpid() < 1)
				return 13;
		}
		uint8_t status = 255;
		if (u_kill(child, TEST_SIGNAL) != 0)
			return 14;
		long waited = 0;
		for (unsigned attempts = 0; waited == 0 && attempts < 2000000; attempts++)
			waited = wait_nohang(child, &status);
		if (waited != child || status != 128 + 14) {
			u_puts("invalid signal context ");
			u_put_u64(broken);
			u_puts(" status ");
			u_put_u64(status);
			u_puts("\n");
			return 14;
		}
	}
	/* SIGKILL still interrupts an AP spin loop after a block-all request. */
	for (unsigned round = 0; round < 20; round++) {
		__atomic_store_n(&shared[0], 0, __ATOMIC_RELEASE);
		long child = u_fork();
		if (child < 0)
			return 6;
		if (child == 0) {
			uint64_t all = UINT64_MAX;
			if (mask(2, &all, NULL) != 0)
				u_exit(16);
			__atomic_store_n(&shared[0], 1, __ATOMIC_RELEASE);
			for (;;)
				__asm__ volatile ("pause");
		}
		for (unsigned attempts = 0;
		    __atomic_load_n(&shared[0], __ATOMIC_ACQUIRE) == 0; attempts++) {
			if (attempts == 2000 || u_sleep_ticks(1) != 0)
				return 7;
		}
		uint8_t status = 255;
		if (u_kill(child, U_SIGKILL) != 0 ||
		    u_waitpid(child, &status) != child || status != 128 + U_SIGKILL)
			return 8;
	}
	if (u_shm_detach(base) != 0)
		return 9;
	u_puts("SMP masked signals fixture passed\n");
	return 0;
}
