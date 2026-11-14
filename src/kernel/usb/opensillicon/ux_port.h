#ifndef OPEN_SILLICON_UX_PORT_H
#define OPEN_SILLICON_UX_PORT_H
#include <stdint.h>
#include <stddef.h>
#include "ux_user.h"
#define VOID void
typedef char CHAR;
typedef unsigned char UCHAR;
typedef int INT;
typedef unsigned int UINT;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int16_t SHORT;
typedef uint16_t USHORT;
typedef int32_t SLONG;
typedef uint64_t ULONG64;
#define SLONG_DEFINED
#define ULONG64_DEFINED
#define ALIGN_TYPE uintptr_t
#define ALIGN_TYPE_DEFINED
#define UX_INTERRUPT_SAVE_AREA ALIGN_TYPE interrupt_save;
#define UX_DISABLE interrupt_save = _ux_utility_interrupt_disable();
#define UX_RESTORE _ux_utility_interrupt_restore(interrupt_save);
#define UX_MAX_SLAVE_CLASS_DRIVER 1
#define UX_MAX_HOST_LUN 1
#define UX_MAX_SLAVE_LUN 1
#define UX_HOST_ENUM_THREAD_STACK_SIZE 8192
#define UX_THREAD_STACK_SIZE 8192
#define UX_THREAD_PRIORITY_ENUM 20
#define UX_THREAD_PRIORITY_CLASS 20
#define UX_THREAD_PRIORITY_KEYBOARD 20
#define UX_THREAD_PRIORITY_HCD 2
#define UX_THREAD_PRIORITY_DCD 2
#define UX_NO_TIME_SLICE 0
#define UX_SLAVE_REQUEST_CONTROL_MAX_LENGTH 256
#define UX_SLAVE_REQUEST_DATA_MAX_LENGTH 2048
#define UX_SYSTEM_THREAD_LOWEST_PRIORITY 31
#define UX_MAX_PRIORITIES 32
static const UCHAR _ux_system_port_version_string[] = "USBX OpenSillicon x86_64 standalone";
_Static_assert(sizeof(ULONG) == 4, "USBX descriptor words must be 32 bits");
_Static_assert(sizeof(ALIGN_TYPE) == sizeof(void *), "USBX alignment must hold a pointer");
#endif
