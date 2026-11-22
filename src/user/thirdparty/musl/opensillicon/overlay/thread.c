#include <errno.h>
#include <sys/syscall.h>
long __os_syscall(long,long,long,long,long,long,long);
int __set_thread_area(void *p) { return __os_syscall(SYS_arch_prctl,0x1002,(long)p,0,0,0,0); }
/* Explicit failure until the kernel supplies thread creation and futexes. */
int __clone(int (*fn)(void *),void *stack,int flags,void *arg,...)
{ (void)fn; (void)stack; (void)flags; (void)arg; return -ENOSYS; }
_Noreturn void __unmapself(void *p, unsigned long n)
{ (void)p; (void)n; __os_syscall(SYS_exit,0,0,0,0,0,0); for (;;) {} }
