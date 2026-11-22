#ifndef ABI_STAT_H
#define ABI_STAT_H

#include <stddef.h>
#include <stdint.h>

/* Fixed-width native file metadata shared by FSTAT and FSTATAT. */
#define OS_S_IFMT 0170000U
#define OS_S_IFREG 0100000U
#define OS_S_IFDIR 0040000U
#define OS_S_IFCHR 0020000U
#define OS_S_IFSOCK 0140000U
#define OS_S_ISREG(mode) (((mode) & OS_S_IFMT) == OS_S_IFREG)
#define OS_S_ISDIR(mode) (((mode) & OS_S_IFMT) == OS_S_IFDIR)

struct os_stat {
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_nlink;
	uint32_t st_mode;
	uint32_t st_uid;
	uint32_t st_gid;
	uint32_t pad0;
	uint64_t st_rdev;
	int64_t st_size;
	int64_t st_blksize;
	int64_t st_blocks;
	int64_t st_atime_sec;
	int64_t st_atime_nsec;
	int64_t st_mtime_sec;
	int64_t st_mtime_nsec;
	int64_t st_ctime_sec;
	int64_t st_ctime_nsec;
	int64_t unused[3];
};

_Static_assert(sizeof(struct os_stat) == 144, "native stat ABI size");
_Static_assert(offsetof(struct os_stat, st_size) == 48, "native stat size offset");
_Static_assert(offsetof(struct os_stat, st_atime_sec) == 72, "native stat timestamp offset");

#endif
