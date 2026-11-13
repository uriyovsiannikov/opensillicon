#include "abi/syscall.h"

.text
.global smp_ap_entry_trampoline
.type smp_ap_entry_trampoline,@function
smp_ap_entry_trampoline:
 cli
 cld
 lidt smp_ap_emergency_idtr(%rip)
	/* Limine jumps here; normalize RSP to the SysV function-entry ABI. */
	andq $-16,%rsp
	subq $8,%rsp
	movq $0,(%rsp)
 jmp smp_ap_entry
.size smp_ap_entry_trampoline,.-smp_ap_entry_trampoline

.global smp_ap_exception_halt
.type smp_ap_exception_halt,@function
smp_ap_exception_halt:
 cli
1: hlt
 jmp 1b
.size smp_ap_exception_halt,.-smp_ap_exception_halt

.global smp_ap_idle
.type smp_ap_idle,@function
smp_ap_idle:
 cli
 movq %rsi,%rsp
 andq $-16,%rsp
 call smp_ap_online
1: hlt
 jmp 1b
.size smp_ap_idle,.-smp_ap_idle

/* Copied to a user executable page. Keep the payload position independent. */
.section .rodata
.global smp_user_probe_start, smp_user_probe_end
smp_user_probe_start:
 movl $0x12345678,%ebx
 movd %ebx,%xmm0
 pushq %rbx
 movl $SYSCALL_GET_ABI_VERSION,%eax
 int $0x80
 jc 1f
 cmpl $SYSCALL_ABI_VERSION,%eax
 jne 1f
 /* A service that uses the BSP process table must fail on the AP. */
 movl $SYSCALL_GETPID,%eax
 int $0x80
 jnc 1f
 popq %rbx
 movd %xmm0,%eax
 cmpl %ebx,%eax
 jne 1f
 xorl %edi,%edi
 jmp 2f
1: movl $1,%edi
2: movl $SYSCALL_EXIT,%eax
 int $0x80
 ud2
smp_user_probe_end:

.section .note.GNU-stack,"",@progbits
