#ifndef ABI_FCNTL_H
#define ABI_FCNTL_H

/* Native OPENAT flags and F_GETFL/F_SETFL status bits. */
#define OS_O_RDONLY 0U
#define OS_O_WRONLY 1U
#define OS_O_RDWR 2U
#define OS_O_ACCMODE 3U
#define OS_O_CREAT 0x40U
#define OS_O_EXCL 0x80U
#define OS_O_TRUNC 0x200U
#define OS_O_APPEND 0x400U
#define OS_O_NONBLOCK 0x800U
#define OS_O_DIRECTORY 0x10000U
#define OS_O_CLOEXEC 0x80000U

#define OS_F_DUPFD 0U
#define OS_F_GETFD 1U
#define OS_F_SETFD 2U
#define OS_F_GETFL 3U
#define OS_F_SETFL 4U
#define OS_F_DUPFD_CLOEXEC 1030U
#define OS_FD_CLOEXEC 1U

#endif
