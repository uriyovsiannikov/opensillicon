/* SPDX-License-Identifier: MIT */
.text
.globl fiber_switch
.type fiber_switch, @function
fiber_switch:
	push %rbp
	push %rbx
	push %r12
	push %r13
	push %r14
	push %r15
	sub $8, %rsp
	stmxcsr (%rsp)
	fnstcw 4(%rsp)
	mov %rsp, (%rdi)
	mov %rsi, %rsp
	ldmxcsr (%rsp)
	fldcw 4(%rsp)
	add $8, %rsp
	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %rbx
	pop %rbp
	ret
.size fiber_switch, .-fiber_switch
.section .note.GNU-stack,"",@progbits
