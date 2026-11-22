#ifndef OPENSILLICON_USER_OS_H
#define OPENSILLICON_USER_OS_H

#include <stddef.h>
#include <stdint.h>

#include "abi/framebuffer.h"
#include "abi/input.h"
#include "abi/credentials.h"
#include "abi/process_info.h"

#define U_EXEC_ARG_MAX 16U
#define U_TIOCSRAW 0x4f530001U
#define U_TIOCCANON 0x4f530002U
#define U_ICMP_ECHO_PAYLOAD_MAX 32U
#define U_POWER_CONTROL_POWEROFF 1U
#define U_POWER_CONTROL_REBOOT 2U

struct u_winsize {
	uint16_t rows;
	uint16_t columns;
	uint16_t xpixel;
	uint16_t ypixel;
};

struct u_icmp_echo_request {
	uint8_t destination[4];
	uint16_t identifier;
	uint16_t sequence;
	uint16_t payload_length;
	uint16_t reserved;
	uint8_t payload[U_ICMP_ECHO_PAYLOAD_MAX];
	uint8_t reply_source[4];
	uint64_t elapsed_ticks;
};

_Static_assert(sizeof(struct u_icmp_echo_request) == 56,
	"OpenSillicon ICMP Echo ABI must be 56 bytes");

long u_write(uint64_t fd, const void *buffer, size_t length);
long u_read(uint64_t fd, void *buffer, size_t length);
long u_ioctl(uint64_t fd, uint64_t request, void *argument);
long u_icmp_echo(struct u_icmp_echo_request *request);
long u_power_control(uint64_t command);
long u_kernel_crash(void);
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
