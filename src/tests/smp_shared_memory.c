/* Shared-object lifetime across concurrent fork, detach and process exit. */
#include "../user/include/os.h"

int user_main(void)
{
	for (unsigned round = 0; round < 12; round++) {
		long base = u_shm_create(4096);
		if (base < 0)
			return 1;
		uint64_t *shared = (void *)base;
		long children[7];
		for (unsigned i = 0; i < 7; i++) {
			children[i] = u_fork();
			if (children[i] < 0)
				return 2;
			if (children[i] == 0) {
				for (unsigned j = 0; j < 60; j++) {
					long temporary = u_shm_create(8192);
					if (temporary < 0)
						u_exit(10);
					uint64_t *data = (void *)temporary;
					if (data[0] != 0 || data[1023] != 0)
						u_exit(11);
					data[0] = i;
					data[1023] = j;
					if (u_shm_detach(temporary) != 0 ||
					    u_shm_detach(temporary) != -22)
						u_exit(12);
				}
				__atomic_store_n(&shared[i], round + 1, __ATOMIC_RELEASE);
				while (__atomic_load_n(&shared[8], __ATOMIC_ACQUIRE) == 0)
					__asm__ volatile ("pause");
				if (u_shm_detach(base) != 0)
					u_exit(13);
				u_exit(0);
			}
		}
		for (unsigned i = 0; i < 7; i++) {
			for (unsigned attempts = 0;
			    __atomic_load_n(&shared[i], __ATOMIC_ACQUIRE) != round + 1;
			    attempts++) {
				if (attempts == 2000 || u_sleep_ticks(1) != 0)
					return 3;
			}
		}
		__atomic_store_n(&shared[8], 1, __ATOMIC_RELEASE);
		/* Children race to release the remaining references to the object. */
		if (u_shm_detach(base) != 0)
			return 4;
		for (unsigned i = 0; i < 7; i++) {
			uint8_t status = 255;
			if (u_waitpid(children[i], &status) != children[i] || status != 0) {
				u_puts("shared child status ");
				u_put_u64(status);
				u_puts("\n");
				return 5;
			}
		}
	}
	u_puts("SMP shared memory fixture passed\n");
	return 0;
}
