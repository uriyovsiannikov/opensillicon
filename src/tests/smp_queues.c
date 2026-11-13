/* Preempt CPU-only workers locally and reclaim queued work for the BSP. */
#include "../user/include/os.h"

#define ROUNDS 16U
#define ITERATIONS 1000000U
#define MULTIPLIER 6364136223846793005ULL
#define INCREMENT 1442695040888963407ULL
#define INITIAL 0x71756575655f736dULL

/* Independent jump-ahead reference for the workers' repeated recurrence. */
static uint64_t expected(unsigned worker)
{
	uint64_t multiplier = 1, increment = 0;
	uint64_t current_multiplier = MULTIPLIER, current_increment = INCREMENT;
	unsigned steps = ROUNDS * ITERATIONS;
	while (steps != 0) {
		if ((steps & 1U) != 0) {
			multiplier *= current_multiplier;
			increment = increment * current_multiplier + current_increment;
		}
		current_increment *= current_multiplier + 1;
		current_multiplier *= current_multiplier;
		steps >>= 1;
	}
	return multiplier * (INITIAL + worker) + increment;
}

static int batch(unsigned workers)
{
	long base = u_shm_create(4096);
	if (base < 0)
		return 1;
	uint64_t *shared = (void *)base;
	long children[15];
	for (unsigned i = 0; i < workers; i++) {
		children[i] = u_fork();
		if (children[i] < 0)
			return 2;
		if (children[i] != 0)
			continue;
		__atomic_store_n(&shared[i], 1, __ATOMIC_RELEASE);
		while (__atomic_load_n(&shared[16], __ATOMIC_ACQUIRE) == 0)
			__asm__ volatile ("pause");
		uint64_t value = INITIAL + i;
		/* No syscalls in this phase: every worker must receive CPU time
		 * to release each barrier, including workers queued behind spinners. */
		for (unsigned round = 0; round < ROUNDS; round++) {
			for (unsigned step = 0; step < ITERATIONS; step++)
				value = value * MULTIPLIER + INCREMENT;
			__atomic_store_n(&shared[32 + i], round + 1, __ATOMIC_RELEASE);
			for (unsigned j = 0; j < workers; j++)
				while (__atomic_load_n(&shared[32 + j], __ATOMIC_ACQUIRE) < round + 1)
					__asm__ volatile ("pause");
		}
		__atomic_store_n(&shared[64 + i], value, __ATOMIC_RELEASE);
		u_exit(0);
	}
	for (unsigned i = 0; i < workers; i++)
		for (unsigned attempts = 0;
		    __atomic_load_n(&shared[i], __ATOMIC_ACQUIRE) == 0; attempts++)
			if (attempts == 2000 || u_sleep_ticks(1) != 0)
				return 3;
	__atomic_store_n(&shared[16], 1, __ATOMIC_RELEASE);
	for (unsigned i = 0; i < workers; i++) {
		uint8_t status = 255;
		if (u_waitpid(children[i], &status) != children[i] || status != 0 ||
		    __atomic_load_n(&shared[32 + i], __ATOMIC_ACQUIRE) != ROUNDS ||
		    __atomic_load_n(&shared[64 + i], __ATOMIC_ACQUIRE) != expected(i)) {
			u_puts("queue child status ");
			u_put_u64(status);
			u_puts("\n");
			return 4;
		}
	}
	if (u_shm_detach(base) != 0)
		return 5;
	return 0;
}

int user_main(void)
{
	int result = batch(4);
	if (result == 0)
		result = batch(15);
	if (result != 0)
		return result;
	u_puts("SMP user queues fixture passed\n");
	return 0;
}
