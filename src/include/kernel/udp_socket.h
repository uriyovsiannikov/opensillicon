#ifndef KERNEL_UDP_SOCKET_H
#define KERNEL_UDP_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#include "abi/inet.h"
#include "kernel/fd.h"
#include "kernel/netdev.h"

#define UDP_SOCKET_LIMIT 8U
#define UDP_SOCKET_QUEUE_LIMIT 4U
#define UDP_SOCKET_PAYLOAD_LIMIT 512U
#define UDP_SOCKET_EPHEMERAL_FIRST 49152U
#define UDP_SOCKET_EPHEMERAL_LAST 65535U
#define UDP_SOCKET_POLL_TIMEOUT_TICKS 500U

struct udp_socket_datagram {
	struct os_ipv4_endpoint source;
	size_t length;
	uint8_t payload[UDP_SOCKET_PAYLOAD_LIMIT];
};

int udp_socket_system_init(struct net_device *device);
int udp_socket_create_fd(struct fd_table *table);
int udp_socket_bind(struct fd_object *object,
	const struct os_ipv4_endpoint *endpoint);
int udp_socket_connect(struct fd_object *object,
	const struct os_ipv4_endpoint *endpoint);
int udp_socket_send(struct fd_object *object, const uint8_t *payload,
	size_t length);
int udp_socket_sendto(struct fd_object *object,
	const struct os_ipv4_endpoint *destination, const uint8_t *payload,
	size_t length);
int udp_socket_peek(struct fd_object *object,
	struct udp_socket_datagram *datagram);
int udp_socket_consume(struct fd_object *object);
int udp_socket_local_endpoint(struct fd_object *object,
	struct os_ipv4_endpoint *endpoint);
int udp_socket_peer_endpoint(struct fd_object *object,
	struct os_ipv4_endpoint *endpoint);
int udp_socket_poll(void);
bool udp_socket_is_object(const struct fd_object *object);
bool udp_socket_system_shutdown(void);

#endif
