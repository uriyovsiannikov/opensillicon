#include <stdint.h>
#include <string.h>
#include <X11/Intrinsic.h>
#include "abi/syscall.h"
#include "xload.h"

struct process_snapshot_entry {
	uint64_t pid;
	uint64_t parent_pid;
	uint32_t state;
	uint8_t exit_status;
	uint8_t reserved[3];
};

static long
process_snapshot(struct process_snapshot_entry *entries, uint64_t capacity)
{
	uint64_t result = SYSCALL_PROCESS_SNAPSHOT;
	unsigned char failed;
	__asm__ volatile ("int $128; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" (entries), "S" (capacity) : "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

void InitLoadPoint(void) {}

void
GetLoadPoint(Widget widget, XtPointer closure, XtPointer call_data)
{
	struct process_snapshot_entry entries[128];
	long count, runnable = 0;
	(void)widget;
	(void)closure;
	count = process_snapshot(entries, 128);
	if (count > 0) {
		for (long i = 0; i < count; i++)
			if (entries[i].state == 0 || entries[i].state == 1)
				runnable++;
	}
	*(double *)call_data = runnable > 0 ? (double)(runnable - 1) : 0.0;
}

void
GetRLoadPoint(Widget widget, XtPointer closure, XtPointer call_data)
{
	(void)widget;
	(void)closure;
	*(double *)call_data = 0.0;
}
