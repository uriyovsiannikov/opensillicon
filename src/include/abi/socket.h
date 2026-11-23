#ifndef ABI_SOCKET_H
#define ABI_SOCKET_H

#define OS_AF_UNIX 1U
#define OS_AF_INET 2U
#define OS_SOCK_STREAM 1U
#define OS_SOCK_DGRAM 2U
#define OS_IPPROTO_TCP 6U
#define OS_IPPROTO_UDP 17U

#define OS_SOL_SOCKET 1U
#define OS_SOL_TCP 6U
#define OS_SO_TYPE 3U
#define OS_SO_ERROR 4U
#define OS_SO_SNDBUF 7U
#define OS_SO_RCVBUF 8U
#define OS_SO_KEEPALIVE 9U
#define OS_SO_PEERCRED 17U
#define OS_SO_PROTOCOL 38U
#define OS_SO_DOMAIN 39U
#define OS_TCP_NODELAY 1U

#define OS_SHUT_RD 0U
#define OS_SHUT_WR 1U
#define OS_SHUT_RDWR 2U

#endif
