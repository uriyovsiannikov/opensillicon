#include <inttypes.h>
#include <stdio.h>
#include <sys/sysctl.h>
#include "abi/process_info.h"

static const char *
state_name(uint32_t state)
{
	switch (state) {
	case OS_PROCESS_READY: return "ready";
	case OS_PROCESS_RUNNING: return "running";
	case OS_PROCESS_BLOCKED: return "blocked";
	case OS_PROCESS_ZOMBIE: return "zombie";
	default: return "unknown";
	}
}

int
main(int argc, char **argv)
{
	struct os_process_info processes[OS_PROCESS_INFO_MAX];
	size_t size = sizeof(processes);
	(void)argv;
	if (argc != 1) {
		fputs("usage: ps\n", stderr);
		return 1;
	}
	if (sysctlbyname("kern.proc.all", processes, &size, NULL, 0) < 0) {
		perror("ps: process snapshot");
		return 1;
	}
	puts("PID PPID UID STATE COMMAND");
	for (size_t i = 0; i < size / sizeof(processes[0]); i++) {
		const struct os_process_info *process = &processes[i];
		printf("%" PRIu64 " %" PRIu64 " %" PRIu32 " %s %.*s\n",
		    process->pid, process->parent_pid, process->uid,
		    state_name(process->state), (int)sizeof(process->name), process->name);
	}
	return fflush(stdout) == EOF || ferror(stdout) ? 1 : 0;
}
