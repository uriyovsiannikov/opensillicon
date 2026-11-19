#include "tls.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

struct pkg_tls {
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config config;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context random;
	mbedtls_x509_crt certificates;
	mbedtls_pk_context key;
	int fd;
	long long deadline;
};

static void tls_error(struct pkg_tls *tls, int result)
{
	char message[192];
	mbedtls_strerror(result, message, sizeof(message));
	fprintf(stderr, "pkg: TLS: %s\n", message);
	uint32_t flags = mbedtls_ssl_get_verify_result(&tls->ssl);
	if (flags && flags != (uint32_t)-1) {
		mbedtls_x509_crt_verify_info(message, sizeof(message), "  ", flags);
		fprintf(stderr, "%s", message);
	}
	errno = EIO;
}

static int tls_remaining(struct pkg_tls *tls)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return -1;
	long long remaining = tls->deadline -
	    ((long long)now.tv_sec * 1000 + now.tv_nsec / 1000000);
	if (remaining <= 0) {
		errno = ETIMEDOUT;
		return -1;
	}
	return (int)remaining;
}

static int tls_wait(struct pkg_tls *tls, int result)
{
	for (;;) {
		int remaining = tls_remaining(tls);
		if (remaining < 0) return -1;
		struct pollfd p = {tls->fd,
		    result == MBEDTLS_ERR_SSL_WANT_WRITE ? POLLOUT : POLLIN, 0};
		int n = poll(&p, 1, (int)remaining);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			if (n == 0) errno = ETIMEDOUT;
			return -1;
		}
		if (p.revents & POLLNVAL) {
			errno = EBADF;
			return -1;
		}
		return 0;
	}
}

void pkg_tls_free(struct pkg_tls *tls)
{
	if (tls == NULL)
		return;
	/* Best effort on the nonblocking socket; never wait during cleanup. */
	(void)mbedtls_ssl_close_notify(&tls->ssl);
	mbedtls_ssl_free(&tls->ssl);
	mbedtls_ssl_config_free(&tls->config);
	mbedtls_x509_crt_free(&tls->certificates);
	mbedtls_pk_free(&tls->key);
	mbedtls_ctr_drbg_free(&tls->random);
	mbedtls_entropy_free(&tls->entropy);
	free(tls);
}

static struct pkg_tls *tls_open(int fd, const char *hostname,
    const char *cert, const char *key, long long deadline)
{
	struct pkg_tls *tls = calloc(1, sizeof(*tls));
	if (tls == NULL)
		return NULL;
	tls->fd = fd;
	tls->deadline = deadline;
	mbedtls_ssl_init(&tls->ssl);
	mbedtls_ssl_config_init(&tls->config);
	mbedtls_entropy_init(&tls->entropy);
	mbedtls_ctr_drbg_init(&tls->random);
	mbedtls_x509_crt_init(&tls->certificates);
	mbedtls_pk_init(&tls->key);
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		goto failed;
	int result = mbedtls_ctr_drbg_seed(&tls->random, mbedtls_entropy_func,
	    &tls->entropy, (const unsigned char *)"OpenSillicon pkg", 16);
	if (result != 0)
		goto tls_failed;
	result = mbedtls_ssl_config_defaults(&tls->config,
	    hostname ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER,
	    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (result != 0)
		goto tls_failed;
	mbedtls_ssl_conf_rng(&tls->config, mbedtls_ctr_drbg_random, &tls->random);
	if (hostname) {
		const char *bundle = "/etc/ssl/cert.pem";
#ifdef PKG_HOST
		const char *override = getenv("SSL_CERT_FILE");
		if (override && *override) bundle = override;
#endif
		result = mbedtls_x509_crt_parse_file(&tls->certificates, bundle);
		/* Partial bundle parsing also fails: no silent trust-store loss. */
		if (result != 0)
			goto tls_failed;
#ifndef PKG_HOST
		/* A separately installed private repository CA supplements public roots. */
		if (access("/etc/ssl/pkg-ca.pem", F_OK) == 0) {
			result = mbedtls_x509_crt_parse_file(&tls->certificates,
			    "/etc/ssl/pkg-ca.pem");
			if (result != 0) goto tls_failed;
		} else if (errno != ENOENT) {
			goto failed;
		}
#endif
		mbedtls_ssl_conf_ca_chain(&tls->config, &tls->certificates, NULL);
		mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_REQUIRED);
	} else {
		result = mbedtls_x509_crt_parse_file(&tls->certificates, cert);
		if (result != 0) goto tls_failed;
		result = mbedtls_pk_parse_keyfile(&tls->key, key, NULL,
		    mbedtls_ctr_drbg_random, &tls->random);
		if (result != 0) goto tls_failed;
		result = mbedtls_ssl_conf_own_cert(&tls->config, &tls->certificates, &tls->key);
		if (result != 0) goto tls_failed;
	}
	result = mbedtls_ssl_setup(&tls->ssl, &tls->config);
	if (result != 0) goto tls_failed;
	if (hostname) {
		result = mbedtls_ssl_set_hostname(&tls->ssl, hostname);
		if (result != 0) goto tls_failed;
	}
	mbedtls_ssl_set_bio(&tls->ssl, &tls->fd, mbedtls_net_send, mbedtls_net_recv, NULL);
	for (;;) {
		if (tls_remaining(tls) < 0) goto failed;
		result = mbedtls_ssl_handshake(&tls->ssl);
		if (result == 0) return tls;
		if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE)
			goto tls_failed;
		if (tls_wait(tls, result) < 0) goto failed;
	}

tls_failed:
	tls_error(tls, result);
failed: {
	int saved = errno;
	pkg_tls_free(tls);
	errno = saved;
	return NULL;
}
}

struct pkg_tls *pkg_tls_connect(int fd, const char *hostname, long long deadline)
{
	return tls_open(fd, hostname, NULL, NULL, deadline);
}

struct pkg_tls *pkg_tls_accept(int fd, const char *cert, const char *key,
    long long deadline)
{
	return tls_open(fd, NULL, cert, key, deadline);
}

void pkg_tls_set_deadline(struct pkg_tls *tls, long long deadline)
{
	tls->deadline = deadline;
}

static ssize_t tls_io(struct pkg_tls *tls, void *buffer, size_t size, int writing)
{
	if (size > 4096) size = 4096;
	for (;;) {
		if (tls_remaining(tls) < 0) return -1;
		int result = writing ? mbedtls_ssl_write(&tls->ssl, buffer, size) :
		    mbedtls_ssl_read(&tls->ssl, buffer, size);
		if (result >= 0) return result;
		if (!writing && result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
		if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
			tls_error(tls, result);
			return -1;
		}
		if (tls_wait(tls, result) < 0) return -1;
	}
}

ssize_t pkg_tls_read(struct pkg_tls *tls, void *buffer, size_t size)
{
	return tls_io(tls, buffer, size, 0);
}

ssize_t pkg_tls_write(struct pkg_tls *tls, const void *buffer, size_t size)
{
	return tls_io(tls, (void *)buffer, size, 1);
}
