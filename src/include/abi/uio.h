#ifndef ABI_UIO_H
#define ABI_UIO_H

#include <stdint.h>

#define OS_IOV_MAX 16U

/* User virtual address and byte count, copied once by readv/writev. */
struct os_iovec {
	uint64_t base;
	uint64_t length;
};

#endif
