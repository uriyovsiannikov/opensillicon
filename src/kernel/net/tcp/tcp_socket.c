#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/cyclone_net.h"
#include "kernel/errno.h"
#include "kernel/fd.h"
#include "kernel/process.h"
#include "kernel/spinlock.h"
#include "kernel/tcp_socket.h"

#include "core/net.h"
#include "core/socket_misc.h"
#include "core/tcp.h"

#define TCP_SOCKET_LIMIT 16U

struct tcp_socket_state {
	bool allocated;
	bool read_shutdown;
	bool write_shutdown;
	bool connect_pending;
	bool no_delay;
	bool reuse_address;
	bool keep_alive;
	int connect_error;
	Socket *socket;
};

static struct tcp_socket_state tcp_sockets[TCP_SOCKET_LIMIT];
static bool tcp_sockets_ready;
static struct spinlock tcp_state_lock = SPINLOCK_INITIALIZER;

static void tcp_socket_close(void *context);

static int
cyclone_error(error_t error)
{
	switch (error) {
	case NO_ERROR: return 0;
	case ERROR_WOULD_BLOCK: return -EAGAIN;
	/* Socket I/O uses DONT_WAIT; Cyclone reports an expired zero timeout
	 * when the operation would block. */
	case ERROR_TIMEOUT: return -EAGAIN;
	case ERROR_CONNECTION_FAILED:
	case ERROR_CONNECTION_REFUSED: return -ECONNREFUSED;
	case ERROR_CONNECTION_RESET: return -ECONNRESET;
	case ERROR_NOT_CONNECTED: return -ENOTCONN;
	case ERROR_ALREADY_CONNECTED: return -EISCONN;
	case ERROR_INVALID_ADDRESS: return -EADDRNOTAVAIL;
	case ERROR_INVALID_PORT: return -EINVAL;
	case ERROR_OUT_OF_MEMORY:
	case ERROR_OUT_OF_RESOURCES: return -ENOBUFS;
	default: return -EIO;
	}
}

static struct tcp_socket_state *
socket_state(struct fd_object *object)
{
	if (!tcp_sockets_ready || object == NULL ||
	    object->operations == NULL || object->operations->close != tcp_socket_close)
		return NULL;
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++)
		if (object->context == &tcp_sockets[i] && tcp_sockets[i].allocated)
			return &tcp_sockets[i];
	return NULL;
}

static int
tcp_socket_read_op(void *context, uint64_t offset, void *buffer,
	size_t length, size_t *read_length)
{
	struct tcp_socket_state *state = context;
	TcpState tcp_state;
	error_t error;
	(void)offset;
	if (state == NULL || !state->allocated || state->socket == NULL)
		return -ENOTCONN;
	tcp_state = tcpGetState(state->socket);
	if (tcp_state == TCP_STATE_LISTEN)
		return -ENOTCONN;
	uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
	bool shutdown = state->read_shutdown;
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	if (shutdown) {
		*read_length = 0;
		return 0;
	}
	error = socketReceive(state->socket, buffer, length, read_length,
	    SOCKET_FLAG_DONT_WAIT);
	if (error == ERROR_END_OF_STREAM) {
		*read_length = 0;
		return 0;
	}
	return cyclone_error(error);
}

static int
tcp_socket_write_op(void *context, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	struct tcp_socket_state *state = context;
	TcpState tcp_state;
	uint64_t flags;
	bool no_delay;
	error_t error;
	(void)offset;
	if (state == NULL || !state->allocated || state->socket == NULL)
		return -ENOTCONN;
	tcp_state = tcpGetState(state->socket);
	if (tcp_state == TCP_STATE_LISTEN)
		return -ENOTCONN;
	flags = spinlock_lock_irqsave(&tcp_state_lock);
	if (state->write_shutdown) {
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		return -EPIPE;
	}
	no_delay = state->no_delay;
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	error = socketSend(state->socket, buffer, length, write_length,
	    SOCKET_FLAG_DONT_WAIT |
	    (no_delay ? SOCKET_FLAG_NO_DELAY : 0));
	/* Cyclone may fill the available buffer before reporting a timeout.
	 * Those bytes are already queued and must not be sent a second time. */
	if (*write_length != 0)
		return 0;
	return cyclone_error(error);
}

static uint32_t
tcp_socket_poll_op(void *context, uint32_t events)
{
	struct tcp_socket_state *state = context;
	TcpState tcp_state;
	uint64_t state_flags;
	uint_t flags;
	uint32_t ready = 0;
	if (state == NULL || !state->allocated || state->socket == NULL)
		return FD_POLL_ERROR;
	/* Cyclone's nonblocking operations replace the event mask with the
	 * events they wait for. Refresh the complete readiness mask before
	 * polling, including after connect has only requested CONNECTED/CLOSED. */
	socketRegisterEvents(state->socket, NULL, SOCKET_EVENT_RX_READY |
	    SOCKET_EVENT_RX_SHUTDOWN | SOCKET_EVENT_CLOSED |
	    SOCKET_EVENT_TX_READY | SOCKET_EVENT_ACCEPT);
	flags = socketGetEvents(state->socket);
	tcp_state = tcpGetState(state->socket);
	state_flags = spinlock_lock_irqsave(&tcp_state_lock);
	if (state->connect_pending && tcp_state == TCP_STATE_CLOSED) {
		state->connect_pending = false;
		state->connect_error = ECONNREFUSED;
	}
	if (tcp_state == TCP_STATE_LISTEN) {
		if ((flags & SOCKET_EVENT_ACCEPT) != 0)
			ready |= FD_POLL_READ;
	} else {
		if (state->read_shutdown || (flags & (SOCKET_EVENT_RX_READY |
		    SOCKET_EVENT_RX_SHUTDOWN | SOCKET_EVENT_CLOSED)) != 0)
			ready |= FD_POLL_READ;
		if (state->write_shutdown || ((tcp_state == TCP_STATE_ESTABLISHED ||
		    tcp_state == TCP_STATE_CLOSE_WAIT) &&
		    (flags & SOCKET_EVENT_TX_READY) != 0))
			ready |= FD_POLL_WRITE;
		if ((flags & SOCKET_EVENT_CLOSED) != 0)
			ready |= FD_POLL_HANGUP;
		if (state->connect_error != 0)
			ready |= FD_POLL_WRITE | FD_POLL_ERROR;
		if ((flags & SOCKET_EVENT_CLOSED) != 0 &&
		    state->socket->resetFlag)
			ready |= FD_POLL_ERROR;
	}
	spinlock_unlock_irqrestore(&tcp_state_lock, state_flags);
	return ready & events;
}

static void
tcp_socket_close(void *context)
{
	uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++) {
		if (context != &tcp_sockets[i])
			continue;
		Socket *socket = tcp_sockets[i].socket;
		tcp_sockets[i] = (struct tcp_socket_state){0};
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		if (socket != NULL)
			socketClose(socket);
		return;
	}
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
}

static const struct fd_operations tcp_socket_operations = {
	.read = tcp_socket_read_op,
	.write = tcp_socket_write_op,
	.poll = tcp_socket_poll_op,
	.close = tcp_socket_close
};

int
tcp_socket_system_init(void)
{
	uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
	if (!cyclone_net_ready() || tcp_sockets_ready) {
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		return -ENETDOWN;
	}
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++)
		tcp_sockets[i] = (struct tcp_socket_state){0};
	tcp_sockets_ready = true;
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	return 0;
}

int
tcp_socket_create_fd(struct fd_table *table)
{
	struct tcp_socket_state *state = NULL;
	struct fd_object *object;
	Socket *socket;
	int descriptor;
	uint64_t flags;
	if (!tcp_sockets_ready || table == NULL)
		return -ENETDOWN;
	flags = spinlock_lock_irqsave(&tcp_state_lock);
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++) {
		if (!tcp_sockets[i].allocated) {
			state = &tcp_sockets[i];
			state->allocated = true;
			break;
		}
	}
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	if (state == NULL)
		return -EMFILE;
	socket = socketOpenEx(netGetDefaultContext(), SOCKET_TYPE_STREAM,
	    SOCKET_IP_PROTO_TCP);
	if (socket == NULL) {
		flags = spinlock_lock_irqsave(&tcp_state_lock);
		*state = (struct tcp_socket_state){0};
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		return -ENOBUFS;
	}
	state->socket = socket;
	object = fd_object_create(&tcp_socket_operations, state);
	if (object == NULL) {
		socketClose(socket);
		flags = spinlock_lock_irqsave(&tcp_state_lock);
		*state = (struct tcp_socket_state){0};
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		return -ENOMEM;
	}
	descriptor = fd_install(table, object);
	if (descriptor < 0) {
		fd_object_put(object);
		return descriptor;
	}
	return descriptor;
}

bool
tcp_socket_is_object(const struct fd_object *object)
{
	uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
	bool result = false;
	if (!tcp_sockets_ready || object == NULL ||
	    object->operations != &tcp_socket_operations)
		goto out;
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++)
		if (object->context == &tcp_sockets[i] && tcp_sockets[i].allocated)
			result = true;
out:
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	return result;
}

int
tcp_socket_bind(struct fd_object *object,
	const struct os_ipv4_endpoint *endpoint)
{
	struct tcp_socket_state *state = socket_state(object);
	IpAddr address = { .length = sizeof(Ipv4Addr) };
	if (state == NULL)
		return -ENOTSOCK;
	if (endpoint == NULL)
		return -EINVAL;
	if (endpoint->port != 0 && endpoint->port < 1024U) {
		struct os_credentials credentials;
		if (!process_current_credentials(&credentials))
			return -ESRCH;
		if (credentials.effective_uid != 0)
			return -EACCES;
	}
	if ((endpoint->address[0] | endpoint->address[1] |
	    endpoint->address[2] | endpoint->address[3]) != 0 &&
	    !cyclone_net_is_local(endpoint->address))
		return -EADDRNOTAVAIL;
	address.ipv4Addr = IPV4_ADDR(endpoint->address[0], endpoint->address[1],
	    endpoint->address[2], endpoint->address[3]);
	return cyclone_error(socketBind(state->socket, &address,
	    endpoint->port != 0 ? endpoint->port : state->socket->localPort));
}

int
tcp_socket_listen(struct fd_object *object, uint64_t backlog)
{
	struct tcp_socket_state *state = socket_state(object);
	int result;
	if (state == NULL)
		return -ENOTSOCK;
	if (backlog > UINT32_MAX)
		return -EINVAL;
	result = cyclone_error(socketListen(state->socket, (uint_t)backlog));
	return result;
}

int
tcp_socket_connect(struct fd_object *object,
	const struct os_ipv4_endpoint *endpoint)
{
	struct tcp_socket_state *state = socket_state(object);
	IpAddr address = { .length = sizeof(Ipv4Addr) };
	uint64_t flags;
	bool connect_pending;
	error_t error;
	if (state == NULL)
		return -ENOTSOCK;
	if (endpoint == NULL || endpoint->port == 0)
		return -EINVAL;
	flags = spinlock_lock_irqsave(&tcp_state_lock);
	connect_pending = state->connect_pending;
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	if (connect_pending) {
		TcpState tcp_state = tcpGetState(state->socket);
		if (tcp_state == TCP_STATE_SYN_SENT ||
		    tcp_state == TCP_STATE_SYN_RECEIVED)
			return -EINPROGRESS;
		flags = spinlock_lock_irqsave(&tcp_state_lock);
		state->connect_pending = false;
		if (tcp_state == TCP_STATE_ESTABLISHED) {
			spinlock_unlock_irqrestore(&tcp_state_lock, flags);
			return 0;
		}
		state->connect_error = ECONNREFUSED;
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		return -ECONNREFUSED;
	}
	if (!cyclone_net_can_route(endpoint->address))
		return -EHOSTUNREACH;
	flags = spinlock_lock_irqsave(&tcp_state_lock);
	state->connect_error = 0;
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	address.ipv4Addr = IPV4_ADDR(endpoint->address[0], endpoint->address[1],
	    endpoint->address[2], endpoint->address[3]);
	(void)socketSetTimeout(state->socket, 0);
	error = socketConnect(state->socket, &address, endpoint->port);
	if (error == ERROR_TIMEOUT &&
	    (tcpGetState(state->socket) == TCP_STATE_SYN_SENT ||
	    tcpGetState(state->socket) == TCP_STATE_SYN_RECEIVED)) {
		flags = spinlock_lock_irqsave(&tcp_state_lock);
		state->connect_pending = true;
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		return -EINPROGRESS;
	}
	return cyclone_error(error);
}

int
tcp_socket_accept(struct fd_table *table, struct fd_object *listener,
	struct os_ipv4_endpoint *peer)
{
	struct tcp_socket_state *parent = socket_state(listener);
	struct tcp_socket_state *child = NULL;
	struct fd_object *object;
	Socket *socket;
	IpAddr address = {0};
	uint16_t port = 0;
	uint64_t flags;
	int descriptor;
	if (parent == NULL || tcpGetState(parent->socket) != TCP_STATE_LISTEN)
		return -EINVAL;
	if (table == NULL)
		return -EINVAL;
	descriptor = fd_find_free_descriptor(table);
	if (descriptor < 0)
		return descriptor;
	/* Reserve wrapper resources before Cyclone removes a queued peer. */
	flags = spinlock_lock_irqsave(&tcp_state_lock);
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++) {
		if (!tcp_sockets[i].allocated) {
			child = &tcp_sockets[i];
			*child = (struct tcp_socket_state){
				.allocated = true,
				.no_delay = parent->no_delay,
				.reuse_address = parent->reuse_address,
				.keep_alive = parent->keep_alive
			};
			break;
		}
	}
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	if (child == NULL)
		return -EMFILE;
	object = fd_object_create(&tcp_socket_operations, child);
	if (object == NULL) {
		tcp_socket_close(child);
		return -ENOMEM;
	}
	descriptor = fd_install(table, object);
	if (descriptor < 0) {
		fd_object_put(object);
		return descriptor;
	}
	socket = socketAccept(parent->socket, &address, &port);
	if (socket == NULL) {
		(void)fd_close(table, (uint64_t)descriptor);
		return -EAGAIN;
	}
	flags = spinlock_lock_irqsave(&tcp_state_lock);
	child->socket = socket;
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	if (peer != NULL) {
		peer->address[0] = (uint8_t)address.ipv4Addr;
		peer->address[1] = (uint8_t)(address.ipv4Addr >> 8);
		peer->address[2] = (uint8_t)(address.ipv4Addr >> 16);
		peer->address[3] = (uint8_t)(address.ipv4Addr >> 24);
		peer->port = port;
	}
	return descriptor;
}

/* Called with tcp_state_lock held. Cyclone's graceful shutdown waits for
 * peer acknowledgments. Keep that transport work in the network service:
 * the descriptor becomes locally shut down as soon as shutdown succeeds. */
static void
tcp_socket_shutdown_progress(struct tcp_socket_state *state)
{
	if (state->read_shutdown) {
		unsigned char discard[512];
		size_t received;
		(void)socketReceive(state->socket, discard, sizeof(discard),
		    &received, SOCKET_FLAG_DONT_WAIT);
	}
	if (state->write_shutdown) {
		TcpState status = tcpGetState(state->socket);
		if (status == TCP_STATE_ESTABLISHED || status == TCP_STATE_CLOSE_WAIT)
			(void)socketShutdown(state->socket, SOCKET_SD_SEND);
	}
}

void
tcp_socket_service(void)
{
	uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++) {
		struct tcp_socket_state *state = &tcp_sockets[i];
		if (state->allocated && state->socket != NULL &&
		    (state->read_shutdown || state->write_shutdown))
			tcp_socket_shutdown_progress(state);
	}
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
}

int
tcp_socket_shutdown(struct fd_object *object, uint64_t how)
{
	struct tcp_socket_state *state = socket_state(object);
	if (state == NULL)
		return -ENOTSOCK;
	if (how > OS_SHUT_RDWR)
		return -EINVAL;
	uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
	TcpState status = tcpGetState(state->socket);
	if (status == TCP_STATE_LISTEN || status == TCP_STATE_SYN_SENT ||
	    (status == TCP_STATE_CLOSED &&
	    !state->read_shutdown && !state->write_shutdown)) {
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		return -ENOTCONN;
	}
	if (how == OS_SHUT_RD || how == OS_SHUT_RDWR)
		state->read_shutdown = true;
	if (how == OS_SHUT_WR || how == OS_SHUT_RDWR)
		state->write_shutdown = true;
	tcp_socket_shutdown_progress(state);
	spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	fd_readiness_wake();
	return 0;
}

int
tcp_socket_get_option(struct fd_object *object, uint64_t level,
	uint64_t option, void *value, size_t *length)
{
	struct tcp_socket_state *state = socket_state(object);
	TcpState tcp_state;
	if (state == NULL)
		return -ENOTSOCK;
	if (value == NULL || length == NULL || *length < sizeof(int))
		return -EINVAL;
	if (level == TCP_SOCKET_SOL_SOCKET) {
		if (option == TCP_SOCKET_SO_ERROR) {
			uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
			bool pending = state->connect_pending;
			spinlock_unlock_irqrestore(&tcp_state_lock, flags);
			if (pending) {
				tcp_state = tcpGetState(state->socket);
				flags = spinlock_lock_irqsave(&tcp_state_lock);
				if (tcp_state == TCP_STATE_ESTABLISHED) {
					state->connect_pending = false;
					state->connect_error = 0;
				} else if (tcp_state == TCP_STATE_CLOSED) {
					state->connect_pending = false;
					state->connect_error = ECONNREFUSED;
				}
				spinlock_unlock_irqrestore(&tcp_state_lock, flags);
			}
			flags = spinlock_lock_irqsave(&tcp_state_lock);
			*(int *)value = state->connect_error;
			state->connect_error = 0;
			spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		} else if (option == TCP_SOCKET_SO_SNDBUF ||
		    option == TCP_SOCKET_SO_RCVBUF ||
		    option == TCP_SOCKET_SO_KEEPALIVE) {
			int result;
			if (option == TCP_SOCKET_SO_KEEPALIVE) {
				uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
				result = state->keep_alive ? 1 : 0;
				spinlock_unlock_irqrestore(&tcp_state_lock, flags);
			} else {
				netLock(state->socket->netContext);
				if (option == TCP_SOCKET_SO_SNDBUF)
					result = (int)state->socket->txBufferSize;
				else
					result = (int)state->socket->rxBufferSize;
				netUnlock(state->socket->netContext);
			}
			*(int *)value = result;
		} else if (option == TCP_SOCKET_SO_REUSEADDR) {
			uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
			*(int *)value = state->reuse_address ? 1 : 0;
			spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		} else {
			return -EOPNOTSUPP;
		}
	} else if (level == TCP_SOCKET_SOL_TCP && option == OS_TCP_DIAG) {
		if (*length < sizeof(struct os_tcp_diag))
			return -EINVAL;
		Socket *socket = state->socket;
		netLock(socket->netContext);
		*(struct os_tcp_diag *)value = (struct os_tcp_diag){
		    .state = socket->state,
		    .receive_window = socket->rcvWnd,
		    .receive_queued = socket->rcvUser,
		    .receive_advanced = socket->rcvNxt - socket->irs - 1,
		    .out_of_order_blocks = socket->sackBlockCount,
		    .rejected_segments = socket->diagRejectedSegments,
		    .ack_sent = socket->diagAckSent,
		    .ack_failed = socket->diagAckFailed,
		    .last_ack_window = socket->diagLastAckWindow,
		};
		netUnlock(socket->netContext);
		*length = sizeof(struct os_tcp_diag);
		return 0;
	} else if (level == TCP_SOCKET_SOL_TCP &&
	    option == TCP_SOCKET_TCP_NODELAY) {
		uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
		*(int *)value = state->no_delay ? 1 : 0;
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
	} else {
		return -EOPNOTSUPP;
	}
	*length = sizeof(int);
	return 0;
}

int
tcp_socket_set_option(struct fd_object *object, uint64_t level,
	uint64_t option, const void *value, size_t length)
{
	struct tcp_socket_state *state = socket_state(object);
	int setting;
	error_t error;
	if (state == NULL)
		return -ENOTSOCK;
	if (value == NULL || length != sizeof(setting))
		return -EINVAL;
	setting = *(const int *)value;
	if (setting != 0 && setting != 1 &&
	    !(level == TCP_SOCKET_SOL_SOCKET &&
	    (option == TCP_SOCKET_SO_SNDBUF || option == TCP_SOCKET_SO_RCVBUF)))
		return -EINVAL;
	if (level == TCP_SOCKET_SOL_SOCKET) {
		switch (option) {
		case TCP_SOCKET_SO_REUSEADDR:
			/* Cyclone does not reject address reuse during bind. */
			{
				uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
				state->reuse_address = setting != 0;
				spinlock_unlock_irqrestore(&tcp_state_lock, flags);
			}
			return 0;
		case TCP_SOCKET_SO_KEEPALIVE:
			error = socketEnableKeepAlive(state->socket,
			    setting ? TRUE : FALSE);
			if (error == NO_ERROR) {
				uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
				state->keep_alive = setting != 0;
				spinlock_unlock_irqrestore(&tcp_state_lock, flags);
			}
			return cyclone_error(error);
		case TCP_SOCKET_SO_SNDBUF:
			if (setting <= 0)
				return -EINVAL;
			error = socketSetTxBufferSize(state->socket, (size_t)setting);
			return error == ERROR_INVALID_PARAMETER ||
			    error == ERROR_INVALID_SOCKET ? -EINVAL :
			    cyclone_error(error);
		case TCP_SOCKET_SO_RCVBUF:
			if (setting <= 0)
				return -EINVAL;
			error = socketSetRxBufferSize(state->socket, (size_t)setting);
			return error == ERROR_INVALID_PARAMETER ||
			    error == ERROR_INVALID_SOCKET ? -EINVAL :
			    cyclone_error(error);
		default:
			return -EOPNOTSUPP;
		}
	}
	if (level == TCP_SOCKET_SOL_TCP &&
	    option == TCP_SOCKET_TCP_NODELAY) {
		uint64_t flags = spinlock_lock_irqsave(&tcp_state_lock);
		state->no_delay = setting != 0;
		spinlock_unlock_irqrestore(&tcp_state_lock, flags);
		return 0;
	}
	return -EOPNOTSUPP;
}

int
tcp_socket_get_name(struct fd_object *object, bool peer,
	struct os_ipv4_endpoint *endpoint)
{
	struct tcp_socket_state *state = socket_state(object);
	TcpState tcp_state;
	Ipv4Addr address;
	uint16_t port;
	if (state == NULL)
		return -ENOTSOCK;
	if (endpoint == NULL)
		return -EINVAL;
	netLock(state->socket->netContext);
	tcp_state = state->socket->state;
	if (peer && tcp_state != TCP_STATE_ESTABLISHED &&
	    tcp_state != TCP_STATE_CLOSE_WAIT &&
	    tcp_state != TCP_STATE_FIN_WAIT_1 &&
	    tcp_state != TCP_STATE_FIN_WAIT_2) {
		netUnlock(state->socket->netContext);
		return -ENOTCONN;
	}
	if (peer) {
		address = state->socket->remoteIpAddr.ipv4Addr;
		port = state->socket->remotePort;
	} else {
		address = state->socket->localIpAddr.ipv4Addr;
		port = state->socket->localPort;
	}
	netUnlock(state->socket->netContext);
	endpoint->address[0] = (uint8_t)address;
	endpoint->address[1] = (uint8_t)(address >> 8);
	endpoint->address[2] = (uint8_t)(address >> 16);
	endpoint->address[3] = (uint8_t)(address >> 24);
	endpoint->port = port;
	return 0;
}
