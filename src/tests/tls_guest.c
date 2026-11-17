#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include "tls_test_ca.h"

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context random_state;
static mbedtls_x509_crt roots, system_roots;

#define CHECK(condition) do { \
	if (!(condition)) { \
		printf("TLS NATIVE FAIL line %d errno %d\n", __LINE__, errno); \
		exit(1); \
	} \
} while (0)

static void
exchange(unsigned port, uint32_t expected_flags)
{
	static mbedtls_ssl_context ssl;
	static mbedtls_ssl_config config;
	struct sockaddr_in address = { .sin_family = AF_INET,
	    .sin_port = htons(port) };
	CHECK(inet_aton("10.15.3.2", &address.sin_addr));
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(fd >= 0);
	CHECK(connect(fd, (const struct sockaddr *)&address, sizeof(address)) == 0);
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&config);
	CHECK(mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
	    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
	mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
	mbedtls_ssl_conf_ca_chain(&config, &roots, NULL);
	mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random_state);
	CHECK(mbedtls_ssl_setup(&ssl, &config) == 0);
	CHECK(mbedtls_ssl_set_hostname(&ssl, "10.15.3.2") == 0);
	mbedtls_ssl_set_bio(&ssl, &fd, mbedtls_net_send, mbedtls_net_recv, NULL);
	mbedtls_ms_time_t deadline = mbedtls_ms_time() + 15000;
	int result;
	do {
		result = mbedtls_ssl_handshake(&ssl);
		if (result != MBEDTLS_ERR_SSL_WANT_READ &&
		    result != MBEDTLS_ERR_SSL_WANT_WRITE)
			break;
		usleep(10000);
	} while (mbedtls_ms_time() < deadline);
	uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
	printf("TLS port %u result=%d verify=%u\n", port, result, flags);
	if (expected_flags != 0) {
		CHECK(result < 0 && (flags & expected_flags) == expected_flags);
	} else {
		CHECK(result == 0 && flags == 0);
		const unsigned char request[] = "GET / HTTP/1.0\r\nHost: 10.15.3.2\r\n\r\n";
		size_t sent = 0;
		while (sent < sizeof(request) - 1 && mbedtls_ms_time() < deadline) {
			result = mbedtls_ssl_write(&ssl, request + sent,
			    sizeof(request) - 1 - sent);
			if (result > 0)
				sent += (size_t)result;
			else
				CHECK(result == MBEDTLS_ERR_SSL_WANT_READ ||
				    result == MBEDTLS_ERR_SSL_WANT_WRITE);
		}
		CHECK(sent == sizeof(request) - 1);
		char response[1024];
		size_t used = 0;
		while (used < sizeof(response) - 1 && mbedtls_ms_time() < deadline) {
			result = mbedtls_ssl_read(&ssl, (unsigned char *)response + used,
			    sizeof(response) - 1 - used);
			if (result > 0) {
				used += (size_t)result;
				response[used] = 0;
				if (strstr(response, "OpenSillicon HTTPS works"))
					break;
			} else if (result == MBEDTLS_ERR_SSL_WANT_READ ||
			    result == MBEDTLS_ERR_SSL_WANT_WRITE) {
				usleep(10000);
			} else {
				break;
			}
		}
		CHECK(used != 0 && strstr(response, "OpenSillicon HTTPS works"));
	}
	close(fd);
	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&config);
}

static int
launch_browser(char *url)
{
	puts("TLS browser waiting for X");
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	static const char path[] = "/tmp/.X11-unix/X0";
	memcpy(address.sun_path + 1, path, sizeof(path) - 1);
	int ready = 0;
	for (unsigned i = 0; i < 300; i++) {
		int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
			return 1;
		ready = connect(fd, (const struct sockaddr *)&address,
		    offsetof(struct sockaddr_un, sun_path) + sizeof(path)) == 0;
		close(fd);
		if (ready)
			break;
		usleep(100000);
	}
	CHECK(ready);
	puts("TLS browser connected to X");
	/* Let startx finish mapping xterm before the test browser is mapped. */
	sleep(8);
	puts("TLS browser executing NetSurf");
	char *arguments[] = {"/usr/bin/netsurf", "--ca_bundle=/tmp/netsurf-test-ca.pem", url, NULL};
	execv(arguments[0], arguments);
	perror("TLS browser exec");
	return 1;
}

int
main(int argc, char **argv)
{
	CHECK(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
	if (argc >= 2 && strcmp(argv[1], "--browser") == 0)
		return launch_browser(argc >= 3 ? argv[2] : "https://10.15.3.2:18443/");
	struct timespec wall, monotonic;
	CHECK(clock_gettime(CLOCK_REALTIME, &wall) == 0);
	CHECK(clock_gettime(CLOCK_MONOTONIC, &monotonic) == 0);
	CHECK(wall.tv_sec >= 1767225600 && monotonic.tv_sec < 3600);
	printf("TLS realtime=%ld monotonic=%ld\n", wall.tv_sec, monotonic.tv_sec);
	mbedtls_x509_crt_init(&roots);
	mbedtls_x509_crt_init(&system_roots);
	CHECK(mbedtls_x509_crt_parse_file(&system_roots, "/etc/ssl/cert.pem") == 0);
	CHECK(mbedtls_x509_crt_parse(&roots, test_ca, sizeof(test_ca)) == 0);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&random_state);
	CHECK(mbedtls_ctr_drbg_seed(&random_state, mbedtls_entropy_func, &entropy,
	    (const unsigned char *)"native TLS test", 15) == 0);
	exchange(18443, 0);
	exchange(18444, MBEDTLS_X509_BADCERT_CN_MISMATCH);
	exchange(18445, MBEDTLS_X509_BADCERT_EXPIRED);
	exchange(18446, MBEDTLS_X509_BADCERT_NOT_TRUSTED);
	/* Supply an extra CA only to the test browser in this disposable VM. */
	FILE *file = fopen("/tmp/netsurf-test-ca.pem", "w");
	FILE *system_file = fopen("/etc/ssl/cert.pem", "r");
	CHECK(file != NULL && system_file != NULL);
	char buffer[4096];
	size_t length;
	while ((length = fread(buffer, 1, sizeof(buffer), system_file)) != 0)
		CHECK(fwrite(buffer, 1, length, file) == length);
	CHECK(!ferror(system_file));
	CHECK(fclose(system_file) == 0);
	CHECK(fwrite(test_ca, 1, sizeof(test_ca) - 1, file) == sizeof(test_ca) - 1);
	CHECK(fclose(file) == 0);
	puts("TLS NATIVE PASS");
	return 0;
}
