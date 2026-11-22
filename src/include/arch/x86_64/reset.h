#ifndef ARCH_X86_64_RESET_H
#define ARCH_X86_64_RESET_H

/* Returning means neither firmware nor the PC reset controller completed reset. */
void arch_reboot(void);

#endif
