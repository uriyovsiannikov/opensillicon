.text
.global __syscall_cp_asm
.hidden __syscall_cp_asm
.global __cp_begin
.hidden __cp_begin
.global __cp_end
.hidden __cp_end
.global __cp_cancel
.hidden __cp_cancel
__syscall_cp_asm:
__cp_begin:
 cmpl $0,(%rdi)
 jne __cp_cancel
 mov %rsi,%rdi
 mov %rdx,%rsi
 mov %rcx,%rdx
 mov %r8,%rcx
 mov %r9,%r8
 mov 8(%rsp),%r9
 mov 16(%rsp),%rax
 sub $8,%rsp
 mov %rax,(%rsp)
 call __os_syscall
 add $8,%rsp
__cp_end:
 ret
__cp_cancel:
 jmp __cancel
.section .note.GNU-stack,"",@progbits
