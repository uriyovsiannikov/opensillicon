#ifndef ABI_ICMP_H
#define ABI_ICMP_H

#include <stddef.h>
#include <stdint.h>

#define OS_ICMP_ECHO_PAYLOAD_MAX 32U

/* IPv4 octets are in network order; integers use native byte order. */
struct os_icmp_echo_request {
	uint8_t destination[4];
	uint16_t identifier;
	uint16_t sequence;
	uint16_t payload_length;
	uint16_t reserved;
	uint8_t payload[OS_ICMP_ECHO_PAYLOAD_MAX];
	uint8_t reply_source[4];
	uint64_t elapsed_ns;
};

_Static_assert(sizeof(struct os_icmp_echo_request) == 56, "ICMP echo ABI size");
_Static_assert(offsetof(struct os_icmp_echo_request, elapsed_ns) == 48, "ICMP echo time offset");

#endif
