#ifndef KERNEL_TCP_SOCKET_H
#define KERNEL_TCP_SOCKET_H

#include <stdbool.h>
#include <stdint.h>

#include "abi/socket.h"
#include "abi/tcp_diag.h"
#include "abi/inet.h"
#include "kernel/fd.h"

#define TCP_SOCKET_SOL_SOCKET 1U
#define TCP_SOCKET_SO_ERROR 4U
#define TCP_SOCKET_SO_SNDBUF 7U
#define TCP_SOCKET_SO_RCVBUF 8U
#define TCP_SOCKET_SO_KEEPALIVE 9U
#define TCP_SOCKET_SO_REUSEADDR 2U
#define TCP_SOCKET_SOL_TCP 6U
#define TCP_SOCKET_TCP_NODELAY 1U

int tcp_socket_system_init(void);
void tcp_socket_service(void);
int tcp_socket_create_fd(struct fd_table *table);
bool tcp_socket_is_object(const struct fd_object *object);
int tcp_socket_bind(struct fd_object *object,
	const struct os_ipv4_endpoint *endpoint);
int tcp_socket_listen(struct fd_object *object, uint64_t backlog);
int tcp_socket_connect(struct fd_object *object,
	const struct os_ipv4_endpoint *endpoint);
int tcp_socket_accept(struct fd_table *table, struct fd_object *listener,
	struct os_ipv4_endpoint *peer);
int tcp_socket_shutdown(struct fd_object *object, uint64_t how);
int tcp_socket_get_option(struct fd_object *object, uint64_t level,
	uint64_t option, void *value, size_t *length);
int tcp_socket_set_option(struct fd_object *object, uint64_t level,
	uint64_t option, const void *value, size_t length);
int tcp_socket_get_name(struct fd_object *object, bool peer,
	struct os_ipv4_endpoint *endpoint);

#endif
