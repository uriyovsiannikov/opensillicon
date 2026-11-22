#include <stdint.h>
#include <string.h>
#include <X11/Intrinsic.h>
#include "abi/syscall.h"
#include "abi/process_info.h"
#include "xload.h"

static long
process_info(struct os_process_info *entries, uint64_t capacity)
{
	uint64_t result = SYSCALL_PROCESS_INFO;
	unsigned char failed;
	__asm__ volatile ("int $128; setc %1" : "+a" (result), "=qm" (failed) :
	    "D" (entries), "S" (capacity) : "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

void InitLoadPoint(void) {}

void
GetLoadPoint(Widget widget, XtPointer closure, XtPointer call_data)
{
	struct os_process_info entries[OS_PROCESS_INFO_MAX];
	long count, runnable = 0;
	(void)widget;
	(void)closure;
	count = process_info(entries, OS_PROCESS_INFO_MAX);
	if (count > 0) {
		for (long i = 0; i < count; i++)
			if (entries[i].state == OS_PROCESS_READY || entries[i].state == OS_PROCESS_RUNNING)
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
