#ifndef KERNEL_UNIX_SOCKET_H
#define KERNEL_UNIX_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "abi/unix_socket.h"
#include "kernel/fd.h"

#define UNIX_SOCKET_SOL_SOCKET 1
#define UNIX_SOCKET_SO_REUSEADDR 2
#define UNIX_SOCKET_SO_SNDBUF 7
#define UNIX_SOCKET_SO_RCVBUF 8
#define UNIX_SOCKET_SO_KEEPALIVE 9
#define UNIX_SOCKET_SO_LINGER 13
#define UNIX_SOCKET_SO_PEERCRED 17
#define UNIX_SOCKET_SHUT_RD 0
#define UNIX_SOCKET_SHUT_WR 1
#define UNIX_SOCKET_SHUT_RDWR 2
#define UNIX_SOCKET_BUFFER_SIZE 65536U

int unix_socket_create(struct fd_table *table);
int unix_socket_bind(struct fd_object *object,
	const struct os_unix_address *address, size_t length);
int unix_socket_get_name(struct fd_object *object, bool peer,
	struct os_unix_address *address, size_t *length);
int unix_socket_listen(struct fd_object *object);
int unix_socket_connect(struct fd_object *object,
	const struct os_unix_address *address, size_t length);
int unix_socket_accept(struct fd_table *table, struct fd_object *listener);
int unix_socket_shutdown(struct fd_object *object, int how);
int unix_socket_get_option(struct fd_object *object, int option,
	void *value, size_t *length);
int unix_socket_set_option(struct fd_object *object, int option,
	const void *value, size_t length);
int unix_socket_bytes_available(struct fd_object *object, size_t *available);
bool unix_socket_is_object(const struct fd_object *object);
void *unix_socket_wait_channel(struct fd_object *object, bool reading);

#endif
