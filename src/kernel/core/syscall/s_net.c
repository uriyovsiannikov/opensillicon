#include "syscall_internal.h"

#include "kernel/cyclone_net.h"
#include "kernel/icmp_echo.h"
#include "kernel/process.h"
#include "kernel/tcp_socket.h"
#include "kernel/udp_socket.h"
#include "kernel/timer.h"
#include "kernel/vmm.h"

struct interrupt_frame *
syscall_net_bsp(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;

	switch (number) {
	case SYSCALL_UDP_SOCKET:
		{
			int result = udp_socket_create_fd(current_fd_table(),
			    frame->rdi, frame->rsi, frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_UDP_BIND:
		{
			struct udp_socket_endpoint endpoint;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (frame->rdx != sizeof(endpoint)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!vmm_copy_from_user(vmm_active_address_space(), &endpoint,
			    frame->rsi, sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = udp_socket_bind(object, &endpoint);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UDP_CONNECT:
		{
			struct udp_socket_endpoint endpoint;
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
			if (frame->rdx != sizeof(endpoint) ||
			    !vmm_copy_from_user(vmm_active_address_space(), &endpoint,
			    frame->rsi, sizeof(endpoint))) {
				syscall_error(frame, frame->rdx != sizeof(endpoint) ?
				    EINVAL : EFAULT);
				break;
			}
			result = udp_socket_connect(object, &endpoint);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
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
			struct udp_socket_endpoint endpoint;
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
			uint64_t start;
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
			    sizeof(struct udp_socket_endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			start = timer_ticks();
			for (;;) {
				result = udp_socket_peek(object, &datagram);
				if (result == 0)
					break;
				if (result != -EAGAIN) {
					syscall_error(frame, errno_from_kernel(result));
					return frame;
				}
				result = udp_socket_poll();
				if (result < 0) {
					syscall_error(frame, errno_from_kernel(result));
					return frame;
				}
				if (timer_ticks() - start >= UDP_SOCKET_POLL_TIMEOUT_TICKS) {
					syscall_error(frame, ETIMEDOUT);
					return frame;
				}
				timer_wait_for_interrupt();
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
	case SYSCALL_TCP_SOCKET:
		{
			int result = tcp_socket_create_fd(current_fd_table(),
			    frame->rdi, frame->rsi, frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_TCP_BIND:
	case SYSCALL_TCP_CONNECT:
		{
			struct tcp_socket_endpoint endpoint;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rdx != sizeof(endpoint) ||
			    !vmm_copy_from_user(vmm_active_address_space(), &endpoint,
			    frame->rsi, sizeof(endpoint))) {
				syscall_error(frame, frame->rdx != sizeof(endpoint) ?
				    EINVAL : EFAULT);
				break;
			}
			result = number == SYSCALL_TCP_BIND ?
			    tcp_socket_bind(object, &endpoint) :
			    tcp_socket_connect(object, &endpoint);
			if (result == -EINPROGRESS && !fd_object_nonblocking(object))
				return process_block_on_channel(frame,
				    fd_readiness_wait_channel());
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_LISTEN:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			result = tcp_socket_listen(object, frame->rsi);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_ACCEPT:
		{
			struct tcp_socket_endpoint peer;
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rsi != 0 && !vmm_validate_user_write(space,
			    frame->rsi, sizeof(peer))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = tcp_socket_accept(current_fd_table(), object,
			    frame->rsi == 0 ? NULL : &peer);
			if (result == -EAGAIN && !fd_object_nonblocking(object))
				return process_block_on_channel(frame,
				    fd_readiness_wait_channel());
			if (result < 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (frame->rsi != 0 && !vmm_copy_to_user(space, frame->rsi,
			    &peer, sizeof(peer))) {
				(void)fd_close(current_fd_table(), (uint64_t)result);
				syscall_error(frame, EFAULT);
			} else {
				frame->rax = (uint64_t)result;
			}
		}
		break;
	case SYSCALL_TCP_SHUTDOWN:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			result = tcp_socket_shutdown(object, frame->rsi);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_GETSOCKOPT:
		{
			int value;
			size_t actual;
			uint32_t length;
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object = fd_object_for_descriptor(
				current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->r8 == 0 || !vmm_copy_from_user(space, &length,
			    frame->r8, sizeof(length)) || length < sizeof(value)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!vmm_validate_user_write(space, frame->r10,
			    sizeof(value)) || !vmm_validate_user_write(space, frame->r8,
			    sizeof(length))) {
				syscall_error(frame, EFAULT);
				break;
			}
			actual = length;
			result = tcp_socket_get_option(object, frame->rsi, frame->rdx,
			    &value, &actual);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			length = (uint32_t)actual;
			if (!vmm_validate_user_write(space, frame->r10, actual) ||
			    !vmm_validate_user_write(space, frame->r8, sizeof(length)) ||
			    !vmm_copy_to_user(space, frame->r10, &value, actual) ||
			    !vmm_copy_to_user(space, frame->r8, &length, sizeof(length))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_SETSOCKOPT:
		{
			int value;
			struct fd_object *object = fd_object_for_descriptor(
				current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->r8 != sizeof(value)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!vmm_copy_from_user(vmm_active_address_space(), &value,
			    frame->r10, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = tcp_socket_set_option(object, frame->rsi,
			    frame->rdx, &value, sizeof(value));
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_GETSOCKNAME:
	case SYSCALL_TCP_GETPEERNAME:
		{
			struct tcp_socket_endpoint endpoint;
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object = fd_object_for_descriptor(
				current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rsi,
			    sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = tcp_socket_get_name(object,
			    number == SYSCALL_TCP_GETPEERNAME, &endpoint);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_copy_to_user(space, frame->rsi, &endpoint,
			    sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_UDP_GETSOCKNAME:
	case SYSCALL_UDP_GETPEERNAME:
		{
			struct udp_socket_endpoint endpoint;
			struct address_space *space = vmm_active_address_space();
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
			if (!vmm_validate_user_write(space, frame->rsi,
			    sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = number == SYSCALL_UDP_GETSOCKNAME ?
			    udp_socket_local_endpoint(object, &endpoint) :
			    udp_socket_peer_endpoint(object, &endpoint);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_copy_to_user(space, frame->rsi, &endpoint,
			    sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_NET_GET_DNS_SERVER:
		{
			uint8_t address[4];
			struct address_space *space = vmm_active_address_space();
			if (!vmm_validate_user_write(space, frame->rdi,
			    sizeof(address))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!cyclone_net_dns_server(address)) {
				syscall_error(frame, ERRNO_EAGAIN);
				break;
			}
			if (!vmm_copy_to_user(space, frame->rdi, address,
			    sizeof(address))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_NET_GET_IPV4_CONFIG:
		{
			uint8_t config[12];
			struct address_space *space = vmm_active_address_space();
			if (!vmm_validate_user_write(space, frame->rdi,
			    sizeof(config))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!cyclone_net_ipv4_config(config, config + 4,
			    config + 8)) {
				syscall_error(frame, ERRNO_EAGAIN);
				break;
			}
			if (!vmm_copy_to_user(space, frame->rdi, config,
			    sizeof(config))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_ICMP_ECHO:
		{
			struct icmp_echo_request request;
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
			request.elapsed_ticks = 0;
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
		syscall_error(frame, ERRNO_ENOSYS);
		break;
	}
	return frame;
}
