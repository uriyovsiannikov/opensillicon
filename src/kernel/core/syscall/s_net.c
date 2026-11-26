#include "syscall_internal.h"
#include <string.h>
#include "abi/socket.h"

#include "kernel/icmp_echo.h"
#include "kernel/process.h"
#include "kernel/tcp_socket.h"
#include "kernel/udp_socket.h"
#include "kernel/unix_socket.h"
#include "kernel/vmm.h"

struct interrupt_frame *
syscall_net_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;

	switch (number) {
	case SYSCALL_SOCKETPAIR:
		{
			int descriptors[2];
			int result;
			if (frame->rdi != OS_AF_UNIX) {
				syscall_error(frame, EAFNOSUPPORT);
				break;
			}
			if (frame->rsi != OS_SOCK_STREAM || frame->rdx != 0) {
				syscall_error(frame, EPROTONOSUPPORT);
				break;
			}
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->r10, sizeof(descriptors))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = unix_socket_pair(current_fd_table(), descriptors);
			if (result < 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->r10,
			    descriptors, sizeof(descriptors))) {
				(void)fd_close(current_fd_table(), descriptors[0]);
				(void)fd_close(current_fd_table(), descriptors[1]);
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_SOCKET_KIND:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			if (object == NULL)
				syscall_error(frame, EBADF);
			else if (unix_socket_is_object(object))
				frame->rax = OS_SOCKET_KIND_UNIX;
			else if (tcp_socket_is_object(object))
				frame->rax = OS_SOCKET_KIND_TCP;
			else if (udp_socket_is_object(object))
				frame->rax = OS_SOCKET_KIND_UDP;
			else
				syscall_error(frame, ENOTSOCK);
		}
		break;
	case SYSCALL_SOCKET:
		{
			uint64_t family = frame->rdi, type = frame->rsi;
			uint64_t protocol = frame->rdx;
			struct fd_table *table = current_fd_table();
			int result;

			if (family == OS_AF_UNIX) {
				result = type == OS_SOCK_STREAM && protocol == 0 ?
				    unix_socket_create(table) : -EPROTONOSUPPORT;
			} else if (family != OS_AF_INET) {
				result = -EAFNOSUPPORT;
			} else if (type == OS_SOCK_STREAM &&
			    (protocol == 0 || protocol == OS_IPPROTO_TCP)) {
				result = tcp_socket_create_fd(table);
			} else if (type == OS_SOCK_DGRAM &&
			    (protocol == 0 || protocol == OS_IPPROTO_UDP)) {
				result = udp_socket_create_fd(table);
			} else {
				result = -EPROTONOSUPPORT;
			}
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_UDP_SEND:
		{
			uint8_t payload[UDP_SOCKET_PAYLOAD_LIMIT];
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!udp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rdx > sizeof(payload) ||
			    (frame->rdx != 0 && !vmm_copy_from_user(
			    vmm_active_address_space(), payload, frame->rsi,
			    (size_t)frame->rdx))) {
				syscall_error(frame, frame->rdx > sizeof(payload) ?
				    EMSGSIZE : EFAULT);
				break;
			}
			result = udp_socket_send(object, payload, (size_t)frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_UDP_SENDTO:
		{
			struct os_ipv4_endpoint endpoint;
			uint8_t payload[UDP_SOCKET_PAYLOAD_LIMIT];
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!udp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rdx > sizeof(payload) || frame->r10 == 0) {
				syscall_error(frame, frame->rdx > sizeof(payload) ?
				    EMSGSIZE : EFAULT);
				break;
			}
			if (!vmm_copy_from_user(vmm_active_address_space(), &endpoint,
			    frame->r10, sizeof(endpoint)) ||
			    (frame->rdx != 0 && !vmm_copy_from_user(
			    vmm_active_address_space(), payload, frame->rsi,
			    (size_t)frame->rdx))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = udp_socket_sendto(object, &endpoint, payload,
			    (size_t)frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_UDP_RECVFROM:
		{
			struct udp_socket_datagram datagram;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!udp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rdx > UDP_SOCKET_PAYLOAD_LIMIT || frame->r10 == 0) {
				syscall_error(frame, frame->rdx > UDP_SOCKET_PAYLOAD_LIMIT ?
				    EMSGSIZE : EFAULT);
				break;
			}
			if ((frame->rdx != 0 && !vmm_validate_user_write(
			    vmm_active_address_space(), frame->rsi,
			    (size_t)frame->rdx)) || !vmm_validate_user_write(
			    vmm_active_address_space(), frame->r10,
			    sizeof(struct os_ipv4_endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = udp_socket_peek(object, &datagram);
			if (result == -EAGAIN && !fd_object_nonblocking(object))
				return process_block_on_channel(frame, fd_readiness_wait_channel());
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				return frame;
			}
			if (datagram.length > frame->rdx) {
				syscall_error(frame, EMSGSIZE);
				break;
			}
			if ((datagram.length != 0 && !vmm_copy_to_user(
			    vmm_active_address_space(), frame->rsi, datagram.payload,
			    datagram.length)) || !vmm_copy_to_user(
			    vmm_active_address_space(), frame->r10, &datagram.source,
			    sizeof(datagram.source))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = udp_socket_consume(object);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = datagram.length;
		}
		break;
	case SYSCALL_BIND:
	case SYSCALL_CONNECT:
		{
			union {
				struct os_ipv4_endpoint inet;
				struct os_unix_address local;
			} address;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			bool local = unix_socket_is_object(object);
			bool tcp = tcp_socket_is_object(object);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!local && !tcp && !udp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (local ? (frame->rdx < sizeof(address.local.family) + 1 ||
			    frame->rdx > sizeof(address.local)) :
			    frame->rdx != sizeof(address.inet)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!vmm_copy_from_user(vmm_active_address_space(), &address,
			    frame->rsi, (size_t)frame->rdx)) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (local)
				result = number == SYSCALL_BIND ?
				    unix_socket_bind(object, &address.local, (size_t)frame->rdx) :
				    unix_socket_connect(object, &address.local, (size_t)frame->rdx);
			else if (tcp)
				result = number == SYSCALL_BIND ?
				    tcp_socket_bind(object, &address.inet) :
				    tcp_socket_connect(object, &address.inet);
			else
				result = number == SYSCALL_BIND ?
				    udp_socket_bind(object, &address.inet) :
				    udp_socket_connect(object, &address.inet);
			if (result == -EINPROGRESS && !fd_object_nonblocking(object))
				return process_block_on_channel(frame,
				    fd_readiness_wait_channel());
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_LISTEN: {
		struct fd_object *object = fd_object_for_descriptor(
		    current_fd_table(), frame->rdi);
		int result;
		if (object == NULL)
			result = -EBADF;
		else if (tcp_socket_is_object(object) || unix_socket_is_object(object)) {
			if (frame->rsi > INT32_MAX)
				result = -EINVAL;
			else if (tcp_socket_is_object(object))
				result = tcp_socket_listen(object, frame->rsi);
			else
				result = unix_socket_listen(object);
		} else
			result = udp_socket_is_object(object) ? -EOPNOTSUPP : -ENOTSOCK;
		if (result != 0)
			syscall_error(frame, errno_from_kernel(result));
		else
			frame->rax = 0;
		break;
	}
	case SYSCALL_ACCEPT: {
		struct address_space *space = vmm_active_address_space();
		struct fd_object *object = fd_object_for_descriptor(
		    current_fd_table(), frame->rdi);
		union { struct os_sockaddr_in inet; uint16_t family; } peer = {0};
		struct os_ipv4_endpoint endpoint;
		bool local = unix_socket_is_object(object);
		bool tcp = tcp_socket_is_object(object);
		uint32_t capacity = 0, actual = local ? sizeof(peer.family) : sizeof(peer.inet);
		int result;
		if (object == NULL) {
			syscall_error(frame, EBADF);
			break;
		}
		if (!local && !tcp) {
			syscall_error(frame, udp_socket_is_object(object) ? EOPNOTSUPP : ENOTSOCK);
			break;
		}
		if (frame->rsi != 0 &&
		    (!vmm_copy_from_user(space, &capacity, frame->rdx, sizeof(capacity)) ||
		    !vmm_validate_user_write(space, frame->rdx, sizeof(actual)) ||
		    (capacity != 0 && !vmm_validate_user_write(space, frame->rsi,
		    capacity < actual ? capacity : actual)))) {
			syscall_error(frame, EFAULT);
			break;
		}
		if (local) {
			peer.family = OS_AF_UNIX;
			result = unix_socket_accept(current_fd_table(), object);
		} else {
			result = tcp_socket_accept(current_fd_table(), object,
			    frame->rsi != 0 ? &endpoint : NULL);
			if (result >= 0 && frame->rsi != 0) {
				peer.inet.family = OS_AF_INET;
				peer.inet.port[0] = (uint8_t)(endpoint.port >> 8);
				peer.inet.port[1] = (uint8_t)endpoint.port;
				memcpy(peer.inet.address, endpoint.address, sizeof(endpoint.address));
			}
		}
		if (result == -EAGAIN && !fd_object_nonblocking(object))
			return process_block_on_channel(frame, local ?
			    unix_socket_wait_channel(object, true) : fd_readiness_wait_channel());
		if (result < 0) {
			syscall_error(frame, errno_from_kernel(result));
			break;
		}
		if (frame->rsi != 0 &&
		    ((capacity != 0 && !vmm_copy_to_user(space, frame->rsi, &peer,
		    capacity < actual ? capacity : actual)) ||
		    !vmm_copy_to_user(space, frame->rdx, &actual, sizeof(actual)))) {
			(void)fd_close(current_fd_table(), (uint64_t)result);
			syscall_error(frame, EFAULT);
		} else
			frame->rax = (uint64_t)result;
		break;
	}
	case SYSCALL_SHUTDOWN: {
		struct fd_object *object = fd_object_for_descriptor(
		    current_fd_table(), frame->rdi);
		int result;
		if (object == NULL)
			result = -EBADF;
		else if (tcp_socket_is_object(object) || unix_socket_is_object(object)) {
			if (frame->rsi > OS_SHUT_RDWR)
				result = -EINVAL;
			else if (tcp_socket_is_object(object))
				result = tcp_socket_shutdown(object, frame->rsi);
			else
				result = unix_socket_shutdown(object, frame->rsi);
		} else
			result = udp_socket_is_object(object) ? -EOPNOTSUPP : -ENOTSOCK;
		if (result != 0)
			syscall_error(frame, errno_from_kernel(result));
		else
			frame->rax = 0;
		break;
	}
	case SYSCALL_GETSOCKOPT: {
		union {
			int integer;
			struct os_tcp_diag diag;
			struct os_unix_ucred credential;
		} value = {0};
		struct address_space *space = vmm_active_address_space();
		struct fd_object *object = fd_object_for_descriptor(
		    current_fd_table(), frame->rdi);
		uint32_t capacity, copied;
		size_t actual = sizeof(value), size = sizeof(int);
		bool tcp = tcp_socket_is_object(object);
		bool local = unix_socket_is_object(object);
		bool udp = udp_socket_is_object(object);
		int result;

		if (object == NULL) {
			syscall_error(frame, EBADF);
			break;
		}
		if (!tcp && !local && !udp) {
			syscall_error(frame, ENOTSOCK);
			break;
		}
		if (!vmm_copy_from_user(space, &capacity, frame->r8, sizeof(capacity)) ||
		    !vmm_validate_user_write(space, frame->r8, sizeof(capacity))) {
			syscall_error(frame, EFAULT);
			break;
		}
		if (tcp && frame->rsi == OS_SOL_TCP && frame->rdx == OS_TCP_DIAG)
			size = sizeof(value.diag);
		else if (local && frame->rsi == OS_SOL_SOCKET &&
		    frame->rdx == OS_SO_PEERCRED)
			size = sizeof(value.credential);
		/* Check output before SO_ERROR can consume a pending error. */
		copied = capacity < size ? capacity : size;
		if (copied != 0 && !vmm_validate_user_write(space, frame->r10, copied)) {
			syscall_error(frame, EFAULT);
			break;
		}
		if (frame->rsi == OS_SOL_SOCKET && (frame->rdx == OS_SO_TYPE ||
		    frame->rdx == OS_SO_DOMAIN || frame->rdx == OS_SO_PROTOCOL)) {
			value.integer = frame->rdx == OS_SO_TYPE ?
			    (udp ? OS_SOCK_DGRAM : OS_SOCK_STREAM) :
			    frame->rdx == OS_SO_DOMAIN ? (local ? OS_AF_UNIX : OS_AF_INET) :
			    (local ? 0 : tcp ? OS_IPPROTO_TCP : OS_IPPROTO_UDP);
			actual = sizeof(value.integer);
			result = 0;
		} else if (tcp)
			result = tcp_socket_get_option(object, frame->rsi, frame->rdx,
			    &value, &actual);
		else if (local && frame->rsi == OS_SOL_SOCKET)
			result = unix_socket_get_option(object, frame->rdx, &value, &actual);
		else
			result = -ENOPROTOOPT;
		if (result != 0) {
			syscall_error(frame, errno_from_kernel(result));
			break;
		}
		copied = capacity < actual ? capacity : actual;
		if ((copied != 0 && !vmm_copy_to_user(space, frame->r10, &value, copied)) ||
		    !vmm_copy_to_user(space, frame->r8, &copied, sizeof(copied)))
			syscall_error(frame, EFAULT);
		else
			frame->rax = 0;
		break;
	}
	case SYSCALL_SETSOCKOPT: {
		struct fd_object *object = fd_object_for_descriptor(
		    current_fd_table(), frame->rdi);
		bool tcp = tcp_socket_is_object(object);
		bool local = unix_socket_is_object(object);
		int value, result;

		if (object == NULL)
			result = -EBADF;
		else if (!tcp && !local)
			result = udp_socket_is_object(object) ? -ENOPROTOOPT : -ENOTSOCK;
		else if (frame->r8 != sizeof(value))
			result = -EINVAL;
		else if (!vmm_copy_from_user(vmm_active_address_space(), &value,
		    frame->r10, sizeof(value)))
			result = -EFAULT;
		else if (tcp)
			result = tcp_socket_set_option(object, frame->rsi, frame->rdx,
			    &value, sizeof(value));
		else if (frame->rsi == OS_SOL_SOCKET)
			result = unix_socket_set_option(object, frame->rdx, &value, sizeof(value));
		else
			result = -ENOPROTOOPT;
		if (result != 0)
			syscall_error(frame, errno_from_kernel(result));
		else
			frame->rax = 0;
		break;
	}
	case SYSCALL_GETSOCKNAME:
	case SYSCALL_GETPEERNAME:
		{
			union {
				struct os_ipv4_endpoint inet;
				struct os_unix_address local;
			} address;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			bool peer = number == SYSCALL_GETPEERNAME;
			size_t length = sizeof(address.inet);
			int result;

			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (unix_socket_is_object(object))
				result = unix_socket_get_name(object, peer, &address.local, &length);
			else if (tcp_socket_is_object(object))
				result = tcp_socket_get_name(object, peer, &address.inet);
			else if (udp_socket_is_object(object))
				result = peer ? udp_socket_peer_endpoint(object, &address.inet) :
				    udp_socket_local_endpoint(object, &address.inet);
			else
				result = -ENOTSOCK;
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			size_t amount = frame->rdx < length ? (size_t)frame->rdx : length;
			if (amount != 0 && !vmm_copy_to_user(vmm_active_address_space(),
			    frame->rsi, &address, amount)) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = length;
		}
		break;
	case SYSCALL_ICMP_ECHO:
		{
			struct os_icmp_echo_request request;
			struct address_space *space = vmm_active_address_space();
			int result;
			if (frame->rdx != sizeof(request)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (frame->rdi == 0 ||
			    !vmm_validate_user_write(space, frame->rdi,
			    sizeof(request)) ||
			    !vmm_copy_from_user(space, &request, frame->rdi,
			    sizeof(request))) {
				syscall_error(frame, EFAULT);
				break;
			}
			for (size_t i = 0; i < sizeof(request.reply_source); i++)
				request.reply_source[i] = 0;
			request.elapsed_ns = 0;
			result = network_icmp_echo(&request);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_copy_to_user(space, frame->rdi, &request,
			    sizeof(request))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	default:
		syscall_error(frame, ENOSYS);
		break;
	}
	return frame;
}
