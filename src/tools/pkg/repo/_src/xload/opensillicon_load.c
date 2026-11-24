#include <stdint.h>
#include <string.h>
#include <X11/Intrinsic.h>
#include <sys/sysctl.h>
#include "abi/process_info.h"
#include "xload.h"

void InitLoadPoint(void) {}

void
GetLoadPoint(Widget widget, XtPointer closure, XtPointer call_data)
{
	struct os_process_info entries[OS_PROCESS_INFO_MAX];
	size_t size = sizeof(entries);
	long runnable = 0;
	(void)widget;
	(void)closure;
	if (sysctlbyname("kern.proc.all", entries, &size, NULL, 0) == 0) {
		for (size_t i = 0; i < size / sizeof(entries[0]); i++)
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
