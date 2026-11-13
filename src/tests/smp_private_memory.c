/* Concurrent private-memory regression for user processes scheduled on APs. */
#include "../user/include/os.h"
#include "abi/syscall.h"

static long call(uint64_t n, uintptr_t a, uintptr_t b, uintptr_t c)
{
    unsigned char carry;
    __asm__ volatile ("int $0x80; setc %1" : "+a"(n), "=qm"(carry)
        : "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return carry ? -(long)n : (long)n;
}

static int exercise(unsigned id)
{
    for (unsigned round = 0; round < 100; round++) {
        long address = call(SYSCALL_MMAP, 12288, 3, 0);
        if (address < 0)
            return 1;
        volatile uint64_t *p = (void *)address;
        for (unsigned i = 0; i < 1536; i++) {
            if (p[i] != 0)
                return 2;
            p[i] = ((uint64_t)id << 40) | ((uint64_t)round << 20) | i;
        }
        if (call(SYSCALL_MPROTECT, address, 12288, 1) != 0)
            return 3;
        for (unsigned i = 0; i < 1536; i++)
            if (p[i] != (((uint64_t)id << 40) | ((uint64_t)round << 20) | i))
                return 4;
        if (call(SYSCALL_MPROTECT, address, 12288, 0) != 0 ||
            call(SYSCALL_MPROTECT, address, 12288, 3) != 0 ||
            call(SYSCALL_MPROTECT, address, 12288, 4) != -95 ||
            call(SYSCALL_MUNMAP, address, 4096, 0) != -22 ||
            call(SYSCALL_MUNMAP, address, 12288, 0) != 0 ||
            call(SYSCALL_MUNMAP, address, 12288, 0) != -22)
            return 5;
    }
    long address = call(SYSCALL_MMAP, 4096, 3, 0);
    if (address < 0)
        return 6;
    volatile uint64_t *p = (void *)address;
    p[0] = id;
    long child = u_fork();
    if (child == 0) {
        if (p[0] != id || call(SYSCALL_MPROTECT, address, 4096, 1) != 0)
            u_exit(66);
        p[0] = 100;
        u_exit(67);
    }
    uint8_t status = 0;
    if (child < 0 || u_waitpid(child, &status) != child ||
        status != 142 || p[0] != id ||
        call(SYSCALL_MUNMAP, address, 4096, 0) != 0)
        return 7;
    return 0;
}

int user_main(void)
{
    long children[7];
    for (unsigned i = 0; i < 7; i++) {
        children[i] = u_fork();
        if (children[i] == 0)
            u_exit(exercise(i + 1));
        if (children[i] < 0)
            return 10;
    }
    int failed = 0;
    for (unsigned i = 0; i < 7; i++) {
        uint8_t status = 255;
        if (u_waitpid(children[i], &status) != children[i] || status != 0) {
            u_puts("memory child status ");
            u_put_u64(status);
            u_puts("\n");
            failed = 1;
        }
    }
    if (!failed)
        u_puts("SMP memory fixture passed\n");
    return failed;
}
