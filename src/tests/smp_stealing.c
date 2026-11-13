/* Imbalanced user queues: preserve registers, SSE and FS base across AP moves. */
#include "../user/include/os.h"
#include "abi/syscall.h"

#define WORKERS 28U
#define MULTIPLIER 6364136223846793005ULL
#define INCREMENT 1442695040888963407ULL
#define INITIAL 0x737465616c5f736dULL
static volatile unsigned handled;

static long call(uint64_t number, uintptr_t a, uintptr_t b, uintptr_t c)
{
	unsigned char carry;
	__asm__ volatile ("int $128; setc %1" : "+a" (number), "=qm" (carry) :
	    "D" (a), "S" (b), "d" (c) : "rcx", "r11", "memory");
	return carry ? -(long)number : (long)number;
}

static void handler(uint64_t signo)
{
	__asm__ volatile ("pxor %%xmm15, %%xmm15" : : : "xmm15");
	if (signo == 10)
		handled++;
}

static uint64_t reference(unsigned worker, uint64_t steps)
{
	uint64_t multiplier = 1, increment = 0, a = MULTIPLIER, b = INCREMENT;
	while (steps != 0) {
		if ((steps & 1U) != 0) {
			multiplier *= a;
			increment = increment * a + b;
		}
		b *= a + 1;
		a *= a;
		steps >>= 1;
	}
	return multiplier * (INITIAL + worker) + increment;
}

int user_main(void)
{
	long base = u_shm_create(4096);
	if (base < 0)
		return 1;
	uint64_t *shared = (void *)base;
	long children[WORKERS];
	for (unsigned i = 0; i < WORKERS; i++) {
		children[i] = u_fork();
		if (children[i] < 0)
			return 2;
		if (children[i] != 0)
			continue;
		struct u_sigaction action = {(uintptr_t)handler, (uintptr_t)u_sigreturn};
		uint64_t tls = INITIAL + i;
		if (u_sigaction(10, &action, NULL) != 0 ||
		    call(SYSCALL_SET_FS_BASE, (uintptr_t)&tls, 0, 0) != 0)
			u_exit(10);
		__atomic_store_n(&shared[i], 1, __ATOMIC_RELEASE);
		while (__atomic_load_n(&shared[32], __ATOMIC_ACQUIRE) == 0)
			__asm__ volatile ("pause");
		/* Gated initial placement groups long jobs while short queues drain. */
		uint64_t steps = i % 7 < 2 ? 64000000 : 40000;
		uint64_t value = INITIAL + i, token[2] = {INITIAL, i}, observed[2], fs_value;
		__asm__ volatile ("movdqu %4, %%xmm15; 1: imul %5, %0; add %6, %0; "
		    "dec %1; jnz 1b; mov %%fs:0, %3; movdqu %%xmm15, %2" :
		    "+&r" (value), "+&r" (steps), "=m" (observed), "=r" (fs_value) :
		    "m" (token), "r" (MULTIPLIER), "r" (INCREMENT) : "cc", "xmm15", "memory");
		if (observed[0] != token[0] || observed[1] != token[1] || fs_value != tls)
			u_exit(11);
		__atomic_store_n(&shared[64 + i], value, __ATOMIC_RELEASE);
		__atomic_store_n(&shared[96 + i], handled, __ATOMIC_RELEASE);
		u_exit(0);
	}
	for (unsigned i = 0; i < WORKERS; i++)
		for (unsigned attempts = 0; __atomic_load_n(&shared[i], __ATOMIC_ACQUIRE) == 0; attempts++)
			if (attempts == 2000 || u_sleep_ticks(1) != 0)
				return 3;
	__atomic_store_n(&shared[32], 1, __ATOMIC_RELEASE);
	for (unsigned round = 0; round < 64; round++)
		for (unsigned i = 0; i < WORKERS; i++) {
			long result = u_kill(children[i], 10);
			if (result != 0 && result != -3)
				return 4;
		}
	for (unsigned i = 0; i < WORKERS; i++) {
		uint8_t status = 255;
		uint64_t steps = i % 7 < 2 ? 64000000 : 40000;
		if (u_waitpid(children[i], &status) != children[i] || status != 0 ||
		    __atomic_load_n(&shared[64 + i], __ATOMIC_ACQUIRE) != reference(i, steps) ||
		    (i % 7 < 2 && __atomic_load_n(&shared[96 + i], __ATOMIC_ACQUIRE) == 0)) {
			u_puts("stealing child status ");
			u_put_u64(status);
			u_puts("\n");
			return 5;
		}
	}
	if (u_shm_detach(base) != 0)
		return 6;
	u_puts("SMP AP stealing fixture passed\n");
	return 0;
}
