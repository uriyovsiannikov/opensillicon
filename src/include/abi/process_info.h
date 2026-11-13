#ifndef OPENSILLICON_ABI_PROCESS_INFO_H
#define OPENSILLICON_ABI_PROCESS_INFO_H

#include <stdint.h>

#define OS_PROCESS_INFO_MAX 128U
#define OS_PROCESS_NAME_SIZE 32U

enum os_process_state {
	OS_PROCESS_READY,
	OS_PROCESS_RUNNING,
	OS_PROCESS_BLOCKED,
	OS_PROCESS_ZOMBIE
};

struct os_process_info {
	uint64_t pid;
	uint64_t parent_pid;
	uint32_t state;
	uint32_t uid;
	char name[OS_PROCESS_NAME_SIZE];
};

_Static_assert(sizeof(struct os_process_info) == 56, "process info ABI size");

#endif
