#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/ssl.h>

mbedtls_ms_time_t
mbedtls_ms_time(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return -1;
	return (mbedtls_ms_time_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int
mbedtls_hardware_poll(void *context, unsigned char *output, size_t length,
    size_t *written)
{
	(void)context;
	*written = 0;
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	while (*written < length) {
		ssize_t n = read(fd, output + *written, length - *written);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			close(fd);
			return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
		}
		*written += (size_t)n;
	}
	close(fd);
	return 0;
}

struct tm *
mbedtls_platform_gmtime_r(const mbedtls_time_t *value, struct tm *result)
{
	struct tm *utc = gmtime(value);
	if (utc == NULL)
		return NULL;
	*result = *utc;
	return result;
}

int
mbedtls_net_send(void *context, const unsigned char *buffer, size_t length)
{
	int fd = *(int *)context;
	ssize_t n = send(fd, buffer, length, 0);
	if (n >= 0)
		return (int)n;
	if (errno == EAGAIN || errno == EINTR)
		return MBEDTLS_ERR_SSL_WANT_WRITE;
	return MBEDTLS_ERR_NET_SEND_FAILED;
}

int
mbedtls_net_recv(void *context, unsigned char *buffer, size_t length)
{
	int fd = *(int *)context;
	ssize_t n = recv(fd, buffer, length, 0);
	if (n >= 0)
		return (int)n;
	if (errno == EAGAIN || errno == EINTR)
		return MBEDTLS_ERR_SSL_WANT_READ;
	return MBEDTLS_ERR_NET_RECV_FAILED;
}
