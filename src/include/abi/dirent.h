#ifndef ABI_DIRENT_H
#define ABI_DIRENT_H

#include <stddef.h>
#include <stdint.h>

#define OS_DT_CHR 2U
#define OS_DT_DIR 4U
#define OS_DT_REG 8U
#define OS_DT_SOCK 12U
#define OS_DIRENT_ALIGNMENT 8U
#define OS_GETDENTS_MAX 4096U

/* Variable-length GETDENTS records, including NUL and zero alignment padding. */
struct os_dirent_record {
	uint64_t inode;
	int64_t offset;
	uint16_t record_length;
	uint8_t type;
	char name[];
};

_Static_assert(offsetof(struct os_dirent_record, name) == 19,
	"native getdents record header ABI");
_Static_assert(_Alignof(struct os_dirent_record) == OS_DIRENT_ALIGNMENT,
	"native getdents record alignment ABI");

#endif
