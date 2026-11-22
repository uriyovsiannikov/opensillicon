#ifndef ABI_INET_H
#define ABI_INET_H

#include <stddef.h>
#include <stdint.h>

/* Native TCP/UDP endpoints: IPv4 octets and a host-order port. */
struct os_ipv4_endpoint {
	uint8_t address[4];
	uint16_t port;
};

_Static_assert(sizeof(struct os_ipv4_endpoint) == 6, "IPv4 endpoint ABI size");
_Static_assert(offsetof(struct os_ipv4_endpoint, port) == 4, "IPv4 port ABI offset");

#endif
