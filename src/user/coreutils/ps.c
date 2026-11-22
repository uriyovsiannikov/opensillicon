#include "../include/os.h"

static const char *
state_name(uint32_t state)
{
	switch (state) {
	case 0: return "ready";
	case 1: return "running";
	case 2: return "blocked";
	case 3: return "zombie";
	default: return "unknown";
	}
}

int
user_main(uint64_t argc, char **argv)
{
	struct os_process_info processes[OS_PROCESS_INFO_MAX];
	long count;
	long i;
	(void)argv;
	if (argc != 1) {
		u_puts("usage: ps\n");
		return 1;
	}
	count = u_process_info(processes, OS_PROCESS_INFO_MAX);
	if (count < 0) {
		u_puts(count == -7 ? "ps: process list exceeds 128 entries\n" :
		    "ps: process snapshot failed\n");
		return 1;
	}
	u_puts("PID PPID UID STATE COMMAND\n");
	for (i = 0; i < count; i++) {
		u_put_u64(processes[i].pid);
		u_puts(" ");
		u_put_u64(processes[i].parent_pid);
		u_puts(" ");
		u_put_u64(processes[i].uid);
		u_puts(" ");
		u_puts(state_name(processes[i].state));
		u_puts(" ");
		u_puts(processes[i].name);
		u_puts("\n");
	}
	return 0;
}
