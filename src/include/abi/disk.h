#ifndef OPENSILLICON_ABI_DISK_H
#define OPENSILLICON_ABI_DISK_H
/* FreeBSD disk ioctl encoding, fixed-width payloads on our x86-64 ABI. */
#define OS_DIOCGSECTORSIZE 0x40046480U /* uint32_t */
#define OS_DIOCGMEDIASIZE  0x40086481U /* int64_t, bytes */
#define OS_DIOCGFLUSH      0x20006487U /* no argument */
#endif
