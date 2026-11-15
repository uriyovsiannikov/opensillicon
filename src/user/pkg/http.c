#include "http.h"
#include "io.h"
#include "tls.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static long long milliseconds(void)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) < 0)
		return -1;
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int ready(int fd, short events, long long deadline)
{
	for (;;) {
		long long now = milliseconds();
		if (now < 0 || now >= deadline) {
			errno = ETIMEDOUT;
			return -1;
		}
		struct pollfd p = {fd, events, 0};
		int r = poll(&p, 1, (int)(deadline - now));
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0) {
			if (r == 0)
				errno = ETIMEDOUT;
			return -1;
		}
		if (p.revents & POLLNVAL) {
			errno = EBADF;
			return -1;
		}
		return 0;
	}
}
static ssize_t receive(int fd, struct pkg_tls *tls, void *p, size_t size, long long deadline)
{
	if (tls != NULL)
		return pkg_tls_read(tls, p, size);
	if (size > 4096)
		size = 4096;
	for (;;) {
		if (ready(fd, POLLIN, deadline) < 0)
			return -1;
		ssize_t n = read(fd, p, size);
		if (n < 0 && (errno == EAGAIN || errno == EINTR))
			continue;
		return n;
	}
}
static int parse_repository(const char *repo, char host[128], char port[6], int *tls)
{
	int secure = strncmp(repo, "https://", 8) == 0;
	if (!secure && strncmp(repo, "http://", 7)) return 0;
	*tls = secure;
	strcpy(port, secure ? "443" : "80");
	const char *start = repo + (secure ? 8 : 7);
	size_t n = strlen(start);
	if (n && start[n - 1] == '/')
		n--;
	const char *colon = memchr(start, ':', n);
	size_t hn = colon ? (size_t)(colon - start) : n;
	if (!hn || hn >= 128)
		return 0;
	memcpy(host, start, hn);
	host[hn] = 0;
	for (size_t i = 0; i < hn; i++)
		if (!((host[i] >= 'a' && host[i] <= 'z') ||
		      (host[i] >= 'A' && host[i] <= 'Z') ||
		      (host[i] >= '0' && host[i] <= '9') || host[i] == '.' ||
		      host[i] == '-'))
			return 0;
	if (colon) {
		size_t pn = n - hn - 1;
		unsigned value = 0;
		if (!pn || pn >= 6)
			return 0;
		memcpy(port, colon + 1, pn);
		port[pn] = 0;
		for (size_t i = 0; i < pn; i++) {
			if (port[i] < '0' || port[i] > '9')
				return 0;
			value = value * 10 + (unsigned)(port[i] - '0');
		}
		if (!value || value > 65535)
			return 0;
	}
	return 1;
}

int pkg_http_repository_valid(const char *repo)
{
	char host[128], port[6];
	int secure;
	return parse_repository(repo, host, port, &secure);
}

uint8_t *pkg_http_get(const char *repo, const char *path, size_t limit, size_t *length)
{
	char host[128], port[6], request[512], header[8193];
	int fd = -1, secure;
	struct pkg_tls *tls = NULL;
	const char *stage = "TCP connect";
	uint8_t *body = NULL;
	struct addrinfo *addresses = NULL;
	if (!parse_repository(repo, host, port, &secure)) {
		fprintf(stderr, "pkg: invalid repository URL (use https://host[:port])\n");
		errno = EINVAL;
		return NULL;
	}
	if (path == NULL || path[0] != '/' || strpbrk(path, "\r\n"))
		goto invalid;
	struct addrinfo hints = {0};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &addresses) != 0) {
		fprintf(stderr, "pkg: cannot resolve repository host %s\n", host);
		return NULL;
	}
	long long now = milliseconds();
	if (now < 0)
		goto failed;
	long long deadline = now + 30000;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		goto failed;
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		goto failed;
	if (connect(fd, addresses->ai_addr, addresses->ai_addrlen) < 0) {
		if (errno != EINPROGRESS && errno != EAGAIN)
			goto failed;
		if (ready(fd, POLLOUT, deadline) < 0)
			goto failed;
		int error = 0;
		socklen_t el = sizeof(error);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &el) < 0)
			goto failed;
		if (error) {
			errno = error;
			goto failed;
		}
	}
	if (secure) {
		stage = "TLS handshake";
		tls = pkg_tls_connect(fd, host, deadline);
		if (tls == NULL) goto failed;
	}
	int rn = snprintf(request, sizeof(request),
			  "GET %s HTTP/1.1\r\nHost: %s:%s\r\nAccept-Encoding: "
			  "identity\r\nConnection: close\r\n\r\n",
			  path, host, port);
	if (rn < 0 || (size_t)rn >= sizeof(request))
		goto invalid;
	stage = "sending request";
	size_t sent = 0;
	while (sent < (size_t)rn) {
		if (ready(fd, POLLOUT, deadline) < 0)
			goto failed;
		ssize_t r = tls ? pkg_tls_write(tls, request + sent, (size_t)rn - sent) :
		    write(fd, request + sent, (size_t)rn - sent);
		if (r < 0 && (errno == EAGAIN || errno == EINTR))
			continue;
		if (r <= 0)
			goto failed;
		sent += (size_t)r;
	}
	stage = "reading response headers";
	size_t used = 0;
	char *end = NULL;
	while (!end) {
		if (used == sizeof(header) - 1) {
			errno = EOVERFLOW;
			goto failed;
		}
		ssize_t r =
		    receive(fd, tls, header + used, sizeof(header) - 1 - used, deadline);
		if (r <= 0) {
			if (r == 0)
				errno = EIO;
			goto failed;
		}
		used += (size_t)r;
		header[used] = 0;
		end = strstr(header, "\r\n\r\n");
	}
	size_t header_length = (size_t)(end - header) + 4;
	if (memchr(header, 0, header_length))
		goto invalid;
	/* The first read may already contain arbitrary binary payload bytes. */
	char *line_end = strstr(header, "\r\n");
	if (!line_end || line_end > end ||
	    (strncmp(header, "HTTP/1.1 200 ", 13) &&
	     strncmp(header, "HTTP/1.0 200 ", 13))) {
		fprintf(stderr,
			"pkg: repository returned an unsuccessful HTTP response\n");
		goto cleanup;
	}
	size_t content_length = 0;
	int have_length = 0;
	char *line = line_end + 2;
	while (line < end) {
		char *next = strstr(line, "\r\n");
		if (!next || next > end)
			goto invalid;
		*next = 0;
		if (!strncasecmp(line, "Content-Length:", 15)) {
			if (have_length)
				goto invalid;
			have_length = 1;
			char *v = line + 15;
			while (*v == ' ' || *v == '\t')
				v++;
			if (!*v)
				goto invalid;
			while (*v >= '0' && *v <= '9') {
				unsigned digit = (unsigned)(*v++ - '0');
				if (digit > limit ||
				    content_length > (limit - digit) / 10)
					goto invalid;
				content_length = content_length * 10 + digit;
			}
			while (*v == ' ' || *v == '\t')
				v++;
			if (*v)
				goto invalid;
		} else if (!strncasecmp(line, "Transfer-Encoding:", 18) ||
			   !strncasecmp(line, "Content-Encoding:", 17))
			goto invalid;
		line = next + 2;
	}
	if (!have_length || content_length > limit ||
	    used - header_length > content_length)
		goto invalid;
	body = malloc(content_length + 1);
	if (!body)
		goto failed;
	stage = "downloading response body";
	size_t received = used - header_length;
	memcpy(body, header + header_length, received);
	while (received < content_length) {
		ssize_t r =
		    receive(fd, tls, body + received, content_length - received, deadline);
		if (r <= 0) {
			if (r == 0)
				errno = EIO;
			goto failed;
		}
		received += (size_t)r;
	}
	body[content_length] = 0;
	*length = content_length;
	pkg_tls_free(tls);
	close(fd);
	freeaddrinfo(addresses);
	return body;
invalid:
	errno = EINVAL;
failed: {
	int error = errno;
	fprintf(stderr, "pkg: %s to %s:%s failed\n", stage, host, port);
	fprintf(stderr, "pkg: repository request failed (errno %d)\n", error);
}
cleanup:
	pkg_tls_free(tls);
	if (fd >= 0)
		close(fd);
	if (addresses)
		freeaddrinfo(addresses);
	free(body);
	return NULL;
}
