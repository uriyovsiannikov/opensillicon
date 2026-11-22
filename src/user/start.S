#include "abi/syscall.h"
.text
.global _start
.type _start, @function
_start:
	xorq %rbp, %rbp
	movq (%rsp), %rdi
	leaq 8(%rsp), %rsi
	movq %rsp, %rdx
	andq $-16, %rsp
	call user_main
	movq %rax, %rdi
	movq $SYSCALL_EXIT, %rax
	int $128
	ud2
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
