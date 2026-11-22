#ifndef OPENSILLICON_USER_OS_H
#define OPENSILLICON_USER_OS_H

#include <stddef.h>
#include <stdint.h>

#define U_AF_INET 2U
#define U_SOCK_STREAM 1U
#define U_IPPROTO_TCP 6U

#include "abi/framebuffer.h"
#include "abi/input.h"
#include "abi/credentials.h"
#include "abi/stat.h"
#include "abi/process_info.h"

#define U_O_RDONLY 0U
#define U_O_WRONLY 1U
#define U_O_RDWR 2U
#define U_O_CREAT 0x40U
#define U_O_EXCL 0x80U
#define U_O_TRUNC 0x200U
#define U_O_DIRECTORY 0x10000U
#define U_SIGINT 2U
#define U_SIGKILL 9U
#define U_SIGTERM 15U
#define U_SIG_DFL 0ULL
#define U_SIG_IGN 1ULL
#define U_EXEC_ARG_MAX 16U
#define U_EXEC_ARG_BYTES_MAX 2048U
#define U_WAIT_NOHANG 1U
#define U_TIOCGWINSZ 0x5413U
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

struct u_ipv4_config {
	uint8_t address[4];
	uint8_t netmask[4];
	uint8_t gateway[4];
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

_Static_assert(sizeof(struct u_ipv4_config) == 12,
	"OpenSillicon IPv4 config ABI must be twelve bytes");
_Static_assert(sizeof(struct u_icmp_echo_request) == 56,
	"OpenSillicon ICMP Echo ABI must be 56 bytes");

long u_write(uint64_t fd, const void *buffer, size_t length);
long u_read(uint64_t fd, void *buffer, size_t length);
long u_open(const char *path, uint64_t flags);
long u_open_mode(const char *path, uint64_t flags, uint32_t mode);
long u_mkdir(const char *path, uint32_t mode);
long u_unlink(const char *path, int directory);
long u_close(uint64_t fd);
long u_stat_path(const char *path, struct os_stat *status);
int u_find_command(const char *name, char *path, size_t capacity);
long u_ioctl(uint64_t fd, uint64_t request, void *argument);
long u_network_ipv4_config(struct u_ipv4_config *config);
long u_network_dns_server(uint8_t address[4]);
long u_icmp_echo(struct u_icmp_echo_request *request);
long u_fork(void);
long u_waitpid(uint64_t pid, int *status);
long u_waitpid_options(uint64_t pid, int *status, uint64_t options);
long u_power_control(uint64_t command);
long u_kernel_crash(void);
long u_kldload(const char *path);
long u_kldunload(const char *name);
long u_chdir(const char *path);
long u_getcwd(char *buffer, size_t capacity);
long u_getpid(void);
long u_process_info(struct os_process_info *entries, size_t capacity);
long u_framebuffer_info(struct os_framebuffer_info *info);
long u_framebuffer_present_rect(const struct os_framebuffer_rect *rect);
long u_kill(uint64_t pid, uint64_t signo);
_Noreturn void u_exit(uint64_t status);
size_t u_strlen(const char *string);
int u_streq(const char *left, const char *right);
void u_puts(const char *string);
void u_putn(const char *string, size_t length);
void u_put_u64(uint64_t value);
int u_normalize_path(const char *cwd, const char *path, char *output,
	size_t capacity);

#endif
