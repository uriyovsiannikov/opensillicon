#ifndef OPENSILLICON_USER_OS_H
#define OPENSILLICON_USER_OS_H

#include <stddef.h>
#include <stdint.h>

#include "abi/framebuffer.h"
#include "abi/input.h"
#include "abi/credentials.h"
#include "abi/process_info.h"

#define U_EXEC_ARG_MAX 16U
#define U_POWER_CONTROL_POWEROFF 1U
#define U_POWER_CONTROL_REBOOT 2U

long u_write(uint64_t fd, const void *buffer, size_t length);
long u_power_control(uint64_t command);
long u_kldload(const char *path);
long u_kldunload(const char *name);
long u_process_info(struct os_process_info *entries, size_t capacity);
long u_framebuffer_info(struct os_framebuffer_info *info);
long u_framebuffer_present_rect(const struct os_framebuffer_rect *rect);
size_t u_strlen(const char *string);
void u_puts(const char *string);
void u_putn(const char *string, size_t length);
void u_put_u64(uint64_t value);

#endif
