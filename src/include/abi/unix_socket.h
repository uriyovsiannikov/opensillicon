#ifndef ABI_UNIX_SOCKET_H
#define ABI_UNIX_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#define OS_UNIX_PATH_MAX 108U

struct os_unix_address {
	uint16_t family;
	uint8_t path[OS_UNIX_PATH_MAX];
};

_Static_assert(sizeof(struct os_unix_address) == 110, "Unix address ABI size");
_Static_assert(offsetof(struct os_unix_address, path) == 2, "Unix path ABI offset");

struct os_unix_ucred {
	int32_t pid;
	uint32_t uid;
	uint32_t gid;
};

#endif
