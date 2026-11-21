#ifndef LIBOS_DNS_H
#define LIBOS_DNS_H
#include <stddef.h>
#include <stdint.h>

/* Validate the answer section and return only addresses belonging to the
 * queried wire name or its CNAME chain. Zero means malformed response. */
int __libos_dns_answers(const uint8_t *, size_t, size_t, unsigned,
    const uint8_t *, size_t, uint8_t (*)[4], size_t, size_t *);
#endif
