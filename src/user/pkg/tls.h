#ifndef OPEN_SILLICON_PKG_TLS_H
#define OPEN_SILLICON_PKG_TLS_H

#include <stddef.h>
#include <sys/types.h>

struct pkg_tls;
/* These functions own TLS state, but the caller retains ownership of fd. */
struct pkg_tls *pkg_tls_connect(int fd, const char *hostname, long long deadline);
struct pkg_tls *pkg_tls_accept(int fd, const char *cert, const char *key,
    long long deadline);
ssize_t pkg_tls_read(struct pkg_tls *, void *, size_t);
ssize_t pkg_tls_write(struct pkg_tls *, const void *, size_t);
void pkg_tls_free(struct pkg_tls *);

#endif
