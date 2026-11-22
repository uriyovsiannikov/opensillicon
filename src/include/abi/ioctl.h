#ifndef ABI_IOCTL_H
#define ABI_IOCTL_H

#include <stdint.h>

/* Byte count currently queued on a Unix stream socket. */
#define OS_FIONREAD 0x541BU
#define OS_TCGETS 0x5401U
#define OS_TCSETS 0x5402U
#define OS_TCSETSW 0x5403U
#define OS_TCSETSF 0x5404U
#define OS_TCSBRK 0x5409U
#define OS_TCFLSH 0x540BU
#define OS_TIOCGWINSZ 0x5413U
#define OS_TIOCSWINSZ 0x5414U
#define OS_FIONBIO 0x5421U

struct os_winsize {
	uint16_t rows;
	uint16_t columns;
	uint16_t xpixel;
	uint16_t ypixel;
};

_Static_assert(sizeof(struct os_winsize) == 8, "terminal window ABI size");
/* Temporarily suppress framebuffer rendering of console output for a GUI session. */
#define OS_TIOCSFB 0x4f530003U
/* Claim (1) or release (0) raw input for the active console graphics session. */
#define OS_TIOCSINPUT 0x4f530004U

#endif
