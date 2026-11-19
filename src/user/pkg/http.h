#ifndef OPEN_SILLICON_PKG_HTTP_H
#define OPEN_SILLICON_PKG_HTTP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
int pkg_http_repository_valid(const char *repository);
uint8_t *pkg_http_get(const char *repository, const char *path, size_t limit,
		      size_t *length, bool show_progress);
#endif
