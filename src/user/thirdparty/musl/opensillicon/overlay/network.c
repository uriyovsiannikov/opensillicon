#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "abi/syscall.h"
#include "abi/inet.h"
long __os_native_call(long,long,long,long,long,long,long);
#define call __os_native_call
static long from_address(long address,long size,struct os_ipv4_endpoint *ep)
{
 if(!address) return -EFAULT;
 if(size<sizeof(struct sockaddr_in)) return -EINVAL;
 const struct sockaddr_in *in=(const void *)address;
 if(in->sin_family!=AF_INET) return -EAFNOSUPPORT;
 memcpy(ep->address,&in->sin_addr,4);
 ep->port=ntohs(in->sin_port);
 return 0;
}
static long to_address(const struct os_ipv4_endpoint *ep,long address,long length)
{
 if(!address) return 0;
 if(!length) return -EFAULT;
 struct sockaddr_in in={.sin_family=AF_INET,.sin_port=htons(ep->port)};
 memcpy(&in.sin_addr,ep->address,4);
 socklen_t *size=(void *)length;
 memcpy((void *)address,&in,*size<sizeof in ? *size : sizeof in);
 *size=sizeof in;
 return 0;
}
static long set_flags(long fd,long flags)
{
 if(fd<0) return fd;
 long r=0;
 if(flags&SOCK_CLOEXEC) r=call(SYSCALL_FCNTL,fd,2,1,0,0,0);
 if(r>=0 && (flags&SOCK_NONBLOCK)) r=call(SYSCALL_FCNTL,fd,4,0x800,0,0,0);
 if(r<0) { call(SYSCALL_CLOSE,fd,0,0,0,0,0); return r; }
 return fd;
}
long __os_network(long n,long a,long b,long c,long d,long e,long f)
{
 if(n==SYS_recvmsg || n==SYS_sendmsg) {
  struct msghdr *msg=(void *)b;
  if(!msg) return -EFAULT;
  if(msg->msg_iovlen>16) return -EMSGSIZE;
  long kind=call(SYSCALL_SOCKET_KIND,a,0,0,0,0,0);
  if(kind<0) return kind;
  if(kind!=OS_SOCKET_KIND_UDP) {
   if(c) return -EOPNOTSUPP;
   if(n==SYS_sendmsg) {
    if(msg->msg_controllen) return -EOPNOTSUPP;
    if(msg->msg_name) return -EISCONN;
    return call(SYSCALL_WRITEV,a,(long)msg->msg_iov,msg->msg_iovlen,0,0,0);
   }
   if(msg->msg_name) return -EOPNOTSUPP;
   long received=call(SYSCALL_READV,a,(long)msg->msg_iov,msg->msg_iovlen,0,0,0);
   if(received>=0) { msg->msg_controllen=0; msg->msg_flags=0; }
   return received;
  }
  size_t total=0;
  for(size_t i=0;i<msg->msg_iovlen;i++) {
   if(msg->msg_iov[i].iov_len>(size_t)INT64_MAX-total) return -EINVAL;
   total+=msg->msg_iov[i].iov_len;
  }
  unsigned char payload[512];
  size_t limit=sizeof payload;
  if(n==SYS_sendmsg) {
   if(msg->msg_controllen) return -EOPNOTSUPP;
   if(total>limit) return -EMSGSIZE;
   size_t size=total<limit ? total : limit, copied=0;
   for(size_t i=0;i<msg->msg_iovlen && copied<size;i++) {
    size_t chunk=msg->msg_iov[i].iov_len;
    if(chunk>size-copied)chunk=size-copied;
    if(chunk)memcpy(payload+copied,msg->msg_iov[i].iov_base,chunk);
    copied+=chunk;
   }
   return __os_network(SYS_sendto,a,(long)payload,size,c,(long)msg->msg_name,msg->msg_namelen);
  }
  if(c & ~MSG_TRUNC) return -EOPNOTSUPP;
  long got=__os_network(SYS_recvfrom,a,(long)payload,
      limit,0,
      (long)msg->msg_name,(long)&msg->msg_namelen);
  if(got<0)return got;
  size_t size=(size_t)got<total ? (size_t)got : total, copied=0;
  for(size_t i=0;i<msg->msg_iovlen && copied<size;i++) {
   size_t chunk=msg->msg_iov[i].iov_len;
   if(chunk>size-copied)chunk=size-copied;
   if(chunk)memcpy(msg->msg_iov[i].iov_base,payload+copied,chunk);
   copied+=chunk;
  }
  msg->msg_controllen=0;
  msg->msg_flags=(size_t)got>total ? MSG_TRUNC : 0;
  return c & MSG_TRUNC ? got : (long)size;
 }
 if(n==SYS_socket) {
  long type=b & ~(SOCK_CLOEXEC|SOCK_NONBLOCK), op;
  if(a==AF_UNIX) op=SYSCALL_UNIX_SOCKET;
  else if(a==AF_INET && type==SOCK_STREAM) op=SYSCALL_TCP_SOCKET;
  else if(a==AF_INET && type==SOCK_DGRAM) op=SYSCALL_UDP_SOCKET;
  else return a==AF_INET ? -EPROTONOSUPPORT : -EAFNOSUPPORT;
  return set_flags(call(op,a,type,c,0,0,0),b);
 }
 switch(n) {
 case SYS_bind: case SYS_connect: case SYS_listen: case SYS_accept:
 case SYS_accept4: case SYS_shutdown: case SYS_getsockopt: case SYS_setsockopt:
 case SYS_getsockname: case SYS_getpeername: case SYS_sendto: case SYS_recvfrom:
  break;
 default: return -ENOSYS;
 }
 long kind=call(SYSCALL_SOCKET_KIND,a,0,0,0,0,0);
 if(kind<0) return kind;
 int unix_socket=kind==OS_SOCKET_KIND_UNIX, tcp=kind==OS_SOCKET_KIND_TCP;
 struct os_ipv4_endpoint ep={0};
 long op, r;
 switch(n) {
 case SYS_bind: case SYS_connect:
  if(unix_socket) return call(n==SYS_bind ? SYSCALL_UNIX_BIND : SYSCALL_UNIX_CONNECT,a,b,c,0,0,0);
  r=from_address(b,c,&ep); if(r<0)return r;
  op=n==SYS_bind ? (tcp ? SYSCALL_TCP_BIND : SYSCALL_UDP_BIND) :
      (tcp ? SYSCALL_TCP_CONNECT : SYSCALL_UDP_CONNECT);
  return call(op,a,(long)&ep,sizeof ep,0,0,0);
 case SYS_listen:
  if(!unix_socket && !tcp) return -EOPNOTSUPP;
  return call(unix_socket ? SYSCALL_UNIX_LISTEN : SYSCALL_TCP_LISTEN,a,b,0,0,0,0);
 case SYS_accept: case SYS_accept4:
  if(n==SYS_accept) d=0;
  if(d & ~(SOCK_CLOEXEC|SOCK_NONBLOCK)) return -EINVAL;
  if(!unix_socket && !tcp) return -EOPNOTSUPP;
  if(b && !c) return -EFAULT;
  r=unix_socket ? call(SYSCALL_UNIX_ACCEPT,a,b,c,0,0,0) :
      call(SYSCALL_TCP_ACCEPT,a,b ? (long)&ep : 0,0,0,0,0);
  if(r>=0 && tcp) {
   long converted=to_address(&ep,b,c);
   if(converted<0) {call(SYSCALL_CLOSE,r,0,0,0,0,0);return converted;}
  }
  return set_flags(r,d);
 case SYS_shutdown:
  if(!unix_socket && !tcp) return -EOPNOTSUPP;
  return call(unix_socket ? SYSCALL_UNIX_SHUTDOWN : SYSCALL_TCP_SHUTDOWN,a,b,0,0,0,0);
 case SYS_getsockopt:
  if(b==SOL_SOCKET && (c==SO_TYPE || c==SO_DOMAIN || c==SO_PROTOCOL)) {
   if(!d || !e) return -EFAULT;
   int value=c==SO_TYPE ? (kind==OS_SOCKET_KIND_UDP ? SOCK_DGRAM : SOCK_STREAM) :
       c==SO_DOMAIN ? (unix_socket ? AF_UNIX : AF_INET) :
       (unix_socket ? 0 : tcp ? IPPROTO_TCP : IPPROTO_UDP);
   socklen_t *size=(void *)e;
   memcpy((void *)d,&value,*size<sizeof value ? *size : sizeof value);
   *size=sizeof value;
   return 0;
  }
  if(!unix_socket && !tcp) return -ENOPROTOOPT;
  return call(unix_socket ? SYSCALL_UNIX_GETSOCKOPT : SYSCALL_TCP_GETSOCKOPT,a,b,c,d,e,0);
 case SYS_setsockopt:
  if(!unix_socket && !tcp) return -ENOPROTOOPT;
  return call(unix_socket ? SYSCALL_UNIX_SETSOCKOPT : SYSCALL_TCP_SETSOCKOPT,a,b,c,d,e,0);
 case SYS_getsockname: case SYS_getpeername:
  if(unix_socket) return -ENOSYS;
  if(!b || !c) return -EFAULT;
  op=n==SYS_getsockname ? (tcp ? SYSCALL_TCP_GETSOCKNAME : SYSCALL_UDP_GETSOCKNAME) :
      (tcp ? SYSCALL_TCP_GETPEERNAME : SYSCALL_UDP_GETPEERNAME);
  r=call(op,a,(long)&ep,0,0,0,0);
  return r<0 ? r : to_address(&ep,b,c);
 case SYS_sendto:
  if(kind==OS_SOCKET_KIND_UDP) d &= ~MSG_NOSIGNAL;
  if(c<0) return -EINVAL;
  if(d) return -EOPNOTSUPP;
  if(unix_socket || tcp) {
   if(e) return -EISCONN;
   return call(SYSCALL_WRITE,a,b,c>4096 ? 4096 : c,0,0,0);
  }
  if(!e) return call(SYSCALL_UDP_SEND,a,b,c,0,0,0);
  r=from_address(e,f,&ep); if(r<0)return r;
  return call(SYSCALL_UDP_SENDTO,a,b,c,(long)&ep,0,0);
 case SYS_recvfrom:
  if(c<0) return -EINVAL;
  if(d) return -EOPNOTSUPP;
  if(unix_socket || tcp) {
   if(e) return -EOPNOTSUPP;
   return call(SYSCALL_READ,a,b,c>4096 ? 4096 : c,0,0,0);
  } else {
   unsigned char payload[512];
   if(e && !f) return -EFAULT;
   r=call(SYSCALL_UDP_RECVFROM,a,(long)payload,sizeof payload,(long)&ep,0,0);
   if(r<0)return r;
   long converted=to_address(&ep,e,f);
   if(converted<0)return converted;
   if(r>c)r=c;
   if(r) memcpy((void *)b,payload,r);
   return r;
  }
 }
 return -ENOSYS;
}
