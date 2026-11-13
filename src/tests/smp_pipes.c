/* Shared pipe queues, atomic records, copy faults and cross-CPU wakeups. */
#include <stdbool.h>
#include "../user/include/os.h"
#include "abi/syscall.h"
#include "abi/process_info.h"

#define WORKERS 7U
#define RECORDS 128U

static long call(uint64_t number, uintptr_t a, uintptr_t b, uintptr_t c)
{
	unsigned char carry;
	__asm__ volatile ("int $128; setc %1" : "+a" (number), "=qm" (carry) :
	    "D" (a), "S" (b), "d" (c) : "rcx", "r11", "memory");
	return carry ? -(long)number : (long)number;
}

static int local_checks(uint32_t *inherited)
{
	uint8_t bytes[4096], result[4096];
	for (unsigned i = 0; i < sizeof(bytes); i++)
		bytes[i] = (uint8_t)(i * 17 + 3);
	if (u_read(inherited[0], result, 1) != -11 ||
	    u_write(inherited[1], bytes, sizeof(bytes)) != sizeof(bytes) ||
	    u_write(inherited[1], bytes, 1) != -11 ||
	    u_read(inherited[0], (void *)1, sizeof(bytes)) != -14 ||
	    u_read(inherited[0], result, sizeof(result)) != sizeof(result))
		return 10;
	for (unsigned i = 0; i < sizeof(bytes); i++)
		if (result[i] != bytes[i])
			return 11;
	/* The BSP's vectored path must use the same synchronized transaction. */
	struct { uint64_t base, length; } vectors[2] = {
		{(uintptr_t)result, 3}, {(uintptr_t)(result + 3), 3}
	};
	if (u_write(inherited[1], bytes, 6) != 6 ||
	    call(SYSCALL_READV, inherited[0], (uintptr_t)vectors, 2) != 6)
		return 12;
	for (unsigned i = 0; i < 6; i++)
		if (result[i] != bytes[i])
			return 13;
	if (u_close(inherited[0]) != 0 || u_write(inherited[1], bytes, 1) != -32 ||
	    u_close(inherited[1]) != 0)
		return 14;
	for (unsigned iteration = 0; iteration < 32; iteration++) {
		uint32_t ends[2];
		if (u_pipe(ends) != 0 || u_write(ends[1], bytes, 8) != 8 ||
		    u_read(ends[0], result, 8) != 8 || u_close(ends[1]) != 0 ||
		    u_read(ends[0], result, 8) != 0 || u_close(ends[0]) != 0)
			return 15;
	}
	return 0;
}

static int wait_for(long child)
{
	uint8_t status = 255;
	if (u_waitpid(child, &status) == child && status == 0)
		return 0;
	u_puts("pipe child status ");
	u_put_u64(status);
	u_puts("\n");
	return 1;
}

static volatile unsigned interrupted;

static void interrupt_handler(uint64_t signo)
{
	if (signo == 10)
		interrupted++;
}

static int wait_blocked(long pid)
{
	struct u_process_info entries[U_PROCESS_SNAPSHOT_MAX];
	for (unsigned attempt = 0; attempt < 2000; attempt++) {
		long count = u_process_snapshot(entries, U_PROCESS_SNAPSHOT_MAX);
		if (count < 0)
			return 1;
		for (long i = 0; i < count; i++)
			if (entries[i].pid == (uint64_t)pid && entries[i].state == OS_PROCESS_BLOCKED)
				return 0;
		if (u_sleep_ticks(1) != 0)
			return 1;
	}
	return 1;
}

static int interrupted_waits(uint64_t *gate)
{
	for (unsigned scenario = 0; scenario < 8; scenario++) {
		uint32_t ends[2];
		uint8_t fill[4096];
		for (unsigned i = 0; i < sizeof(fill); i++)
			fill[i] = 0xa5;
		bool writing = (scenario & 1U) != 0;
		bool vector = (scenario & 2U) != 0;
		bool killing = (scenario & 4U) != 0;
		__atomic_store_n(gate, 0, __ATOMIC_RELEASE);
		if (u_pipe(ends) != 0 || (writing && u_write(ends[1], fill, sizeof(fill)) != sizeof(fill)))
			return 30;
		long child = u_fork();
		if (child < 0)
			return 31;
		if (child == 0) {
			struct u_sigaction action = {(uintptr_t)interrupt_handler, (uintptr_t)u_sigreturn};
			if (u_sigaction(10, &action, NULL) != 0 || u_close(ends[writing ? 0 : 1]) != 0)
				u_exit(32);
			uint8_t byte = 7;
			uint32_t fd = ends[writing ? 1 : 0];
			struct { uint64_t base, length; } iov = {(uintptr_t)&byte, 1};
			long result = vector ? call(writing ? SYSCALL_WRITEV : SYSCALL_READV,
			    fd, (uintptr_t)&iov, 1) : writing ? u_write(fd, &byte, 1) : u_read(fd, &byte, 1);
			if (killing || result != -4 || interrupted != 1)
				u_exit(33);
			__atomic_store_n(gate, 1, __ATOMIC_RELEASE);
			if ((writing ? u_write(fd, &byte, 1) : u_read(fd, &byte, 1)) != 1 || byte != 7)
				u_exit(34);
			if (u_close(fd) != 0)
				u_exit(35);
			u_exit(0);
		}
		if (u_close(ends[writing ? 1 : 0]) != 0 || wait_blocked(child) != 0 ||
		    u_kill(child, killing ? U_SIGKILL : 10) != 0)
			return 36;
		uint32_t fd = ends[writing ? 0 : 1];
		if (!killing) {
			for (unsigned attempts = 0; __atomic_load_n(gate, __ATOMIC_ACQUIRE) == 0; attempts++)
				if (attempts == 2000 || u_sleep_ticks(1) != 0)
					return 37;
			uint8_t byte = 7;
			if (writing) {
				if (u_read(fd, fill, sizeof(fill)) != sizeof(fill))
					return 38;
				for (unsigned i = 0; i < sizeof(fill); i++)
					if (fill[i] != 0xa5)
						return 39;
				if (u_read(fd, &byte, 1) != 1 || byte != 7)
					return 40;
			} else if (u_write(fd, &byte, 1) != 1) {
				return 41;
			}
		}
		uint8_t status = 255;
		if (u_waitpid(child, &status) != child || status != (killing ? 137 : 0) || u_close(fd) != 0)
			return 42;
	}
	return 0;
}

int user_main(void)
{
	uint32_t ends[2];
	if (u_pipe(ends) != 0 || call(SYSCALL_FCNTL, ends[0], 4, 0x800) != 0 ||
	    call(SYSCALL_FCNTL, ends[1], 4, 0x800) != 0)
		return 1;
	long gate_base = u_shm_create(4096);
	if (gate_base < 0)
		return 2;
	uint64_t *gate = (void *)gate_base;
	long check = u_fork();
	if (check < 0)
		return 2;
	if (check == 0) {
		while (__atomic_load_n(gate, __ATOMIC_ACQUIRE) == 0)
			__asm__ volatile ("pause");
		u_exit(local_checks(ends));
	}
	/* Drop parent references before the child checks final-reader EPIPE. */
	if (u_close(ends[0]) != 0 || u_close(ends[1]) != 0)
		return 3;
	__atomic_store_n(gate, 1, __ATOMIC_RELEASE);
	if (wait_for(check) != 0 || interrupted_waits(gate) != 0 || u_shm_detach(gate_base) != 0)
		return 3;

	for (unsigned round = 0; round < 4; round++) {
		long base = u_shm_create(8192);
		if (base < 0 || u_pipe(ends) != 0)
			return 4;
		uint64_t *seen = (void *)base;
		long children[2 * WORKERS];
		for (unsigned i = 0; i < 2 * WORKERS; i++) {
			children[i] = u_fork();
			if (children[i] < 0)
				return 5;
			if (children[i] != 0)
				continue;
			if (i < WORKERS) {
				if (u_close(ends[0]) != 0)
					u_exit(20);
				for (unsigned sequence = 0; sequence < RECORDS; sequence++) {
					uint64_t record[3] = {i, sequence, (uint64_t)i * 100000 + sequence + 17};
					struct { uint64_t base, length; } vectors[2] = {
						{(uintptr_t)record, 8}, {(uintptr_t)(record + 1), 16}
					};
					long amount = sequence % 2 == 0 ? u_write(ends[1], record, sizeof(record)) :
					    call(SYSCALL_WRITEV, ends[1], (uintptr_t)vectors, 2);
					if (amount != sizeof(record))
						u_exit(21);
				}
				if (u_close(ends[1]) != 0)
					u_exit(22);
			} else {
				if (u_close(ends[1]) != 0)
					u_exit(23);
				for (;;) {
					uint64_t record[3];
					long amount = u_read(ends[0], record, sizeof(record));
					if (amount == 0)
						break;
					if (amount != sizeof(record) || record[0] >= WORKERS ||
					    record[1] >= RECORDS ||
					    record[2] != record[0] * 100000 + record[1] + 17)
						u_exit(24);
					unsigned index = record[0] * RECORDS + record[1];
					if (__atomic_fetch_add(&seen[index], 1, __ATOMIC_RELAXED) != 0)
						u_exit(25);
				}
				if (u_close(ends[0]) != 0)
					u_exit(26);
			}
			u_exit(0);
		}
		if (u_close(ends[0]) != 0 || u_close(ends[1]) != 0)
			return 6;
		for (unsigned i = 0; i < 2 * WORKERS; i++)
			if (wait_for(children[i]) != 0)
				return 7;
		for (unsigned i = 0; i < WORKERS * RECORDS; i++)
			if (__atomic_load_n(&seen[i], __ATOMIC_RELAXED) != 1)
				return 8;
		if (u_shm_detach(base) != 0)
			return 9;
	}
	u_puts("SMP pipes fixture passed\n");
	return 0;
}
