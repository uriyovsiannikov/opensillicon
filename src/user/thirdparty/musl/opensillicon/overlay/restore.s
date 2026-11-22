.text
.global __restore
.hidden __restore
.global __restore_rt
.hidden __restore_rt
.type __restore_rt,@function
__restore:
__restore_rt:
 mov $26,%eax
 int $0x80
 ud2
.section .note.GNU-stack,"",@progbits
