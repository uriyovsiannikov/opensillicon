#ifndef OPENSILLICON_ABI_DISK_H
#define OPENSILLICON_ABI_DISK_H
/* FreeBSD disk ioctl encoding, fixed-width payloads on our x86-64 ABI. */
#define OS_DIOCGSECTORSIZE 0x40046480U /* uint32_t */
#define OS_DIOCGMEDIASIZE  0x40086481U /* int64_t, bytes */
#define OS_DIOCGFLUSH      0x20006487U /* no argument */
/* OpenSillicon extension: geometry and GPT identity, no user pointers. */
#include <stdint.h>
#define OS_DIOCGPARTINFO 0x40584f10U
#define OS_DISK_PARTITION 1U
#define OS_DISK_MOUNTED 2U
#define OS_DISK_RAW_WRITE_ALLOWED 4U
struct os_disk_partinfo {
 uint64_t media_bytes, first_sector;
 uint32_t partition_number, flags;
 char parent[32];
 uint8_t uuid[16], type_uuid[16];
};
_Static_assert(sizeof(struct os_disk_partinfo)==88, "disk partition info ABI");
#endif
