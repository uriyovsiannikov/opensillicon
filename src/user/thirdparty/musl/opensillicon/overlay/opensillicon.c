/* Translate musl's Linux syscall vocabulary to OpenSillicon ABI v26.
 * Never forward unknown numbers: the two number spaces overlap. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <poll.h>
#include "abi/poll.h"
#include "abi/uio.h"

#include <sys/stat.h>
#include <dirent.h>
#include <stddef.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stdint.h>
#include <termios.h>
#include <time.h>
#include <sys/ioctl.h>
#include "abi/syscall.h"
#include "abi/fcntl.h"
_Static_assert(LOCK_SH == OS_LOCK_SH && LOCK_EX == OS_LOCK_EX &&
    LOCK_NB == OS_LOCK_NB && LOCK_UN == OS_LOCK_UN, "flock ABI");
#include "abi/time.h"
#include "abi/dirent.h"
#include "abi/credentials.h"
#include "abi/termios.h"
#include "abi/ioctl.h"
#include "abi/stat.h"
#include "abi/signal.h"
_Static_assert(sizeof(struct iovec) == sizeof(struct os_iovec), "iovec ABI size");
_Static_assert(sizeof(struct pollfd) == sizeof(struct os_pollfd), "pollfd ABI size");
#define CHECK_FIELD(type, field, native, member) \
 _Static_assert(offsetof(struct type, field) == offsetof(struct native, member), #type " " #field " offset"); \
 _Static_assert(sizeof(((struct type *)0)->field) == sizeof(((struct native *)0)->member), #type " " #field " width")
CHECK_FIELD(iovec, iov_base, os_iovec, base);
CHECK_FIELD(iovec, iov_len, os_iovec, length);
CHECK_FIELD(pollfd, fd, os_pollfd, fd);
CHECK_FIELD(pollfd, events, os_pollfd, events);
CHECK_FIELD(pollfd, revents, os_pollfd, revents);
#undef CHECK_FIELD
#define CHECK_POLL(name) _Static_assert(name == OS_##name, #name " poll ABI")
CHECK_POLL(POLLIN);
CHECK_POLL(POLLOUT);
CHECK_POLL(POLLERR);
CHECK_POLL(POLLHUP);
CHECK_POLL(POLLNVAL);
CHECK_POLL(POLLRDNORM);
CHECK_POLL(POLLWRNORM);
#undef CHECK_POLL

extern long __os_raw(long,long,long,long,long,long,long);
long __os_native_call(long n,long a,long b,long c,long d,long e,long f)
{
 long r=__os_raw(n,a,b,c,d,e,f);
 return r == -78 ? -ENOSYS : r;
}
#define call __os_native_call
long __os_network(long,long,long,long,long,long,long);

#define CHECK_IOCTL(name) _Static_assert(name == OS_##name, #name " ioctl ABI")
CHECK_IOCTL(TCGETS);
CHECK_IOCTL(TCSETS);
CHECK_IOCTL(TCSETSW);
CHECK_IOCTL(TCSETSF);
CHECK_IOCTL(TCSBRK);
CHECK_IOCTL(TCFLSH);
CHECK_IOCTL(TIOCGWINSZ);
CHECK_IOCTL(TIOCSWINSZ);
CHECK_IOCTL(FIONREAD);
CHECK_IOCTL(FIONBIO);
#undef CHECK_IOCTL
_Static_assert(sizeof(struct winsize) == sizeof(struct os_winsize), "winsize ABI size");
#define CHECK_WINDOW(public, native) \
 _Static_assert(offsetof(struct winsize, public) == offsetof(struct os_winsize, native), "winsize " #public " offset"); \
 _Static_assert(sizeof(((struct winsize *)0)->public) == sizeof(((struct os_winsize *)0)->native), "winsize " #public " width")
CHECK_WINDOW(ws_row, rows);
CHECK_WINDOW(ws_col, columns);
CHECK_WINDOW(ws_xpixel, xpixel);
CHECK_WINDOW(ws_ypixel, ypixel);
#undef CHECK_WINDOW

/* The native stat contract and musl layout must agree at this boundary. */
_Static_assert(sizeof(struct os_termios)==56, "termios ABI size");
_Static_assert(sizeof(struct stat)==sizeof(struct os_stat), "stat ABI size");
_Static_assert(offsetof(struct stat, st_mode)==offsetof(struct os_stat, st_mode), "stat mode ABI");
_Static_assert(offsetof(struct stat, st_size)==offsetof(struct os_stat, st_size), "stat size ABI");
_Static_assert(offsetof(struct stat, st_atim)==offsetof(struct os_stat, st_atime_sec), "stat timestamps ABI");
#define CHECK_DIRENT_FIELD(public, native) \
 _Static_assert(offsetof(struct dirent, public)==offsetof(struct os_dirent_record, native), "dirent " #public " offset ABI"); \
 _Static_assert(sizeof(((struct dirent *)0)->public)==sizeof(((struct os_dirent_record *)0)->native), "dirent " #public " width ABI")
CHECK_DIRENT_FIELD(d_ino, inode);
CHECK_DIRENT_FIELD(d_off, offset);
CHECK_DIRENT_FIELD(d_reclen, record_length);
CHECK_DIRENT_FIELD(d_type, type);
#undef CHECK_DIRENT_FIELD
_Static_assert(offsetof(struct dirent, d_name)==offsetof(struct os_dirent_record, name), "dirent name ABI");
_Static_assert(_Alignof(struct dirent)==OS_DIRENT_ALIGNMENT, "dirent alignment ABI");
_Static_assert(DT_CHR==OS_DT_CHR && DT_DIR==OS_DT_DIR && DT_REG==OS_DT_REG && DT_SOCK==OS_DT_SOCK, "dirent types ABI");

_Static_assert(sizeof(struct timespec)==sizeof(struct os_timespec), "timespec ABI size");
_Static_assert(offsetof(struct timespec, tv_sec)==offsetof(struct os_timespec, tv_sec), "timespec seconds ABI");
_Static_assert(offsetof(struct timespec, tv_nsec)==offsetof(struct os_timespec, tv_nsec), "timespec nanoseconds ABI");
_Static_assert(sizeof(((struct timespec *)0)->tv_sec)==sizeof(int64_t), "timespec seconds width");
_Static_assert(sizeof(((struct timespec *)0)->tv_nsec)==sizeof(int64_t), "timespec nanoseconds width");
_Static_assert(CLOCK_REALTIME==OS_CLOCK_REALTIME, "realtime clock ABI");
_Static_assert(CLOCK_MONOTONIC==OS_CLOCK_MONOTONIC, "monotonic clock ABI");

/* Matching flag values need no per-bit translation. */
#define CHECK_OPEN_FLAG(name) _Static_assert(name == OS_##name, #name " ABI")
CHECK_OPEN_FLAG(O_RDONLY);
CHECK_OPEN_FLAG(O_WRONLY);
CHECK_OPEN_FLAG(O_RDWR);
/* musl includes O_PATH in O_ACCMODE; native VFS rejects O_PATH unchanged. */
_Static_assert((O_ACCMODE & ~O_PATH) == OS_O_ACCMODE, "access mode ABI");
CHECK_OPEN_FLAG(O_CREAT);
CHECK_OPEN_FLAG(O_EXCL);
CHECK_OPEN_FLAG(O_TRUNC);
CHECK_OPEN_FLAG(O_APPEND);
CHECK_OPEN_FLAG(O_NONBLOCK);
CHECK_OPEN_FLAG(O_DIRECTORY);
CHECK_OPEN_FLAG(O_CLOEXEC);
#undef CHECK_OPEN_FLAG

static long open_native(long dirfd, long path, long flags, long mode)
{
 /* No symlinks exist in this VFS. O_NOFOLLOW is consequently satisfied.
  * Large file offsets are native. Never silently discard other flags. */
 const long allowed=O_ACCMODE|O_CREAT|O_EXCL|O_TRUNC|O_DIRECTORY|
     O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW|O_LARGEFILE|O_APPEND|O_NOCTTY;
 if(flags & ~allowed) return -EOPNOTSUPP;
 return call(SYSCALL_OPENAT,dirfd,path,
     flags & ~(O_NOFOLLOW|O_LARGEFILE|O_NOCTTY),mode,0,0);
}

#define CHECK_FCNTL_VALUE(name) _Static_assert(name == OS_##name, #name " ABI")
CHECK_FCNTL_VALUE(F_DUPFD);
CHECK_FCNTL_VALUE(F_DUPFD_CLOEXEC);
CHECK_FCNTL_VALUE(F_GETFD);
CHECK_FCNTL_VALUE(F_SETFD);
CHECK_FCNTL_VALUE(F_GETFL);
CHECK_FCNTL_VALUE(F_SETFL);
CHECK_FCNTL_VALUE(FD_CLOEXEC);
#undef CHECK_FCNTL_VALUE

static long fcntl_native(long fd,long cmd,long arg)
{
 switch(cmd) {
 case F_GETFD: case F_GETFL:
  return call(SYSCALL_FCNTL,fd,cmd,0,0,0,0);
 case F_SETFD:
  if(arg & ~FD_CLOEXEC) return -EINVAL;
  /* Descriptor flags and duplicate commands share the native vocabulary. */
 case F_DUPFD: case F_DUPFD_CLOEXEC:
  return call(SYSCALL_FCNTL,fd,cmd,arg,0,0,0);
 case F_SETFL:
  if(arg & ~(O_ACCMODE|O_NONBLOCK|O_LARGEFILE|O_APPEND)) return -EOPNOTSUPP;
  return call(SYSCALL_FCNTL,fd,cmd,arg & (O_NONBLOCK|O_APPEND),0,0,0);
 default: return -ENOSYS;
 }
}
/* Translate musl x86-64 sigaction to the native wire format. */
_Static_assert(sizeof(struct os_signal_action)==32, "native sigaction ABI");
struct linux_action { uint64_t handler, flags, restorer, mask; };

static long action_native(long signo,long input,long output,long size)
{
 if(size!=8) return -EINVAL;
 struct os_signal_action action={0}, previous={0};
 if(input) {
  const struct linux_action *linux=(const void *)input;
  /* Restart, siginfo and alternate stacks follow later. */
  uint64_t flags=(uint32_t)linux->flags;
  if(linux->handler>1 && (flags & ~(0x04000000UL | OS_SA_RESETHAND | OS_SA_NODEFER)))
   return -EOPNOTSUPP;
  action.handler=linux->handler;
  action.flags=flags & (OS_SA_RESETHAND | OS_SA_NODEFER);
  action.mask=linux->mask << 1;
  action.restorer=linux->handler>1 ? linux->restorer : 0;
 }
 long r=call(SYSCALL_SIGACTION,signo,input ? (long)&action : 0,
     output ? (long)&previous : 0,0,0,0);
 if(!r && output) {
  struct linux_action *linux=(void *)output;
  *linux=(struct linux_action){.handler=previous.handler,.restorer=previous.restorer,
      .mask=previous.mask >> 1,.flags=previous.flags};
 }
 return r;
}
long __os_syscall(long n,long a,long b,long c,long d,long e,long f)
{
 long s;
 switch(n) {
 case SYS_exit: case SYS_exit_group: s=SYSCALL_EXIT; break;
 case SYS_getpid: case SYS_gettid: s=SYSCALL_GETPID; break;
 case SYS_fork: s=SYSCALL_FORK; break;
 case SYS_wait4:
  if(d) return -ENOSYS; /* Kernel has no per-child resource accounting yet. */
  s=SYSCALL_WAITPID; break;
 case SYS_kill: case SYS_tkill: s=SYSCALL_KILL; break;
 case SYS_rt_sigaction: return action_native(a,b,c,d);
 case SYS_rt_sigprocmask: {
  if(d!=8) return -EINVAL;
  /* Native masks use bit signo, Linux masks use bit signo-1. */
  uint64_t requested=b ? *(const uint64_t *)b << 1 : 0, previous=0;
  long r=call(SYSCALL_SIGPROCMASK,a,b ? (long)&requested : 0,
      c ? (long)&previous : 0,0,0,0);
  if(!r && c) *(uint64_t *)c=previous >> 1;
  return r;
 }
 case SYS_getuid: case SYS_geteuid: case SYS_getgid: case SYS_getegid: {
  struct os_credentials creds;
  long r=call(SYSCALL_GETCREDENTIALS,(long)&creds,0,0,0,0,0);
  if(r<0) return r;
  return n==SYS_getuid ? creds.real_uid : n==SYS_geteuid ? creds.effective_uid :
      n==SYS_getgid ? creds.real_gid : creds.effective_gid;
 }
 case SYS_setuid: s=SYSCALL_SETUID; break;
 case SYS_setgid: s=SYSCALL_SETGID; break;
 case SYS_setitimer: s=SYSCALL_SETITIMER; break;
 case SYS_getitimer: s=SYSCALL_GETITIMER; break;
 case SYS_chown: return call(SYSCALL_CHOWN,a,(uint32_t)b,(uint32_t)c,0,0,0);
 case SYS_fchown: return call(SYSCALL_FCHOWN,a,(uint32_t)b,(uint32_t)c,0,0,0);
 case SYS_setresuid:
  return call(SYSCALL_SETRESUID,(uint32_t)a,(uint32_t)b,(uint32_t)c,0,0,0);
 case SYS_setresgid:
  return call(SYSCALL_SETRESGID,(uint32_t)a,(uint32_t)b,(uint32_t)c,0,0,0);
 case SYS_ioctl: {
  if(b>=TCGETS && b<=TCSETSF) {
   if(!c) return -EFAULT;
   struct os_termios native={0};
   struct termios *tio=(void *)c;
   if(b!=TCGETS) {
    if(tio->c_line) return -EINVAL;
    native.input_flags=tio->c_iflag; native.output_flags=tio->c_oflag;
    native.control_flags=tio->c_cflag; native.local_flags=tio->c_lflag;
    memcpy(native.control_chars,tio->c_cc,32);
    native.input_speed=tio->__c_ispeed; native.output_speed=tio->__c_ospeed;
   }
   long r=call(SYSCALL_IOCTL,a,b,(long)&native,0,0,0);
   if(!r && b==TCGETS) {
    *tio=(struct termios){.c_iflag=native.input_flags,.c_oflag=native.output_flags,
        .c_cflag=native.control_flags,.c_lflag=native.local_flags,
        .__c_ispeed=native.input_speed,.__c_ospeed=native.output_speed};
    memcpy(tio->c_cc,native.control_chars,32);
   }
   return r;
  }
  /* Other requests retain their native encoding: winsize, byte counts,
   * nonblocking mode and OpenSillicon disk/audio/framebuffer extensions. */
  return call(SYSCALL_IOCTL,a,(unsigned int)b,c,0,0,0);
 }
 case SYS_poll: s=SYSCALL_POLL; break;
 case SYS_select: s=SYSCALL_SELECT; break;
 case SYS_pipe2: s=SYSCALL_PIPE2; break;
 case SYS_pipe: return call(SYSCALL_PIPE2,a,0,0,0,0,0);
 case SYS_getppid: s=SYSCALL_GETPPID; break;
 case SYS_read: s=SYSCALL_READ; break;
 case SYS_write: s=SYSCALL_WRITE; break;
 case SYS_readv: s=SYSCALL_READV; break;
 case SYS_writev: s=SYSCALL_WRITEV; break;
 case SYS_open: return open_native(AT_FDCWD,a,b,c);
 case SYS_openat: return open_native(a,b,c,d);
 case SYS_flock: s=SYSCALL_FLOCK; break;
 case SYS_fcntl: return fcntl_native(a,b,c);
 case SYS_dup: return call(SYSCALL_FCNTL,a,0,0,0,0,0);
 case SYS_dup3: s=SYSCALL_DUP3; break;
 case SYS_dup2:
  if(a==b) {
   long r=call(SYSCALL_FCNTL,a,1,0,0,0,0);
   return r<0 ? r : a;
  }
  return call(SYSCALL_DUP3,a,b,0,0,0,0);
 case SYS_readlink: case SYS_readlinkat: {
  /* The VFS has no symbolic links. Existence/permissions must still be
   * checked so realpath can distinguish ordinary nodes from missing paths. */
  long fd=n==SYS_readlink ? AT_FDCWD : a;
  long path=n==SYS_readlink ? a : b;
  struct stat st;
  long r=call(SYSCALL_FSTATAT,fd,path,(long)&st,AT_SYMLINK_NOFOLLOW,0,0);
  return r<0 ? r : -EINVAL;
 }
 case SYS_fstat: s=SYSCALL_FSTAT; break;
 case SYS_newfstatat: s=SYSCALL_FSTATAT; break;
 case SYS_stat: return call(SYSCALL_FSTATAT,AT_FDCWD,a,b,0,0,0);
 case SYS_lstat: return call(SYSCALL_FSTATAT,AT_FDCWD,a,b,AT_SYMLINK_NOFOLLOW,0,0);
 case SYS_getdents64:
  if(c>OS_GETDENTS_MAX)c=OS_GETDENTS_MAX;
  s=SYSCALL_GETDENTS; break;
 case SYS_mkdir: s=SYSCALL_MKDIR; break;
 case SYS_unlink: return call(SYSCALL_UNLINK,a,0,0,0,0,0);
 case SYS_rmdir: return call(SYSCALL_UNLINK,a,1,0,0,0,0);
 case SYS_mount:
  if(!c || strcmp((const char *)c,"silliconfs")) return -ENODEV;
  return call(SYSCALL_MOUNT,a,b,d,0,0,0);
 case SYS_umount2: s=SYSCALL_UNMOUNT; break;
 case SYS_access: s=SYSCALL_ACCESS; break;
 case SYS_chmod: s=SYSCALL_CHMOD; break;
 case SYS_fchmod: s=SYSCALL_FCHMOD; break;
 case SYS_umask: s=SYSCALL_UMASK; break;
 case SYS_close: s=SYSCALL_CLOSE; break;
 case SYS_lseek: s=SYSCALL_LSEEK; break;
 case SYS_fsync: s=SYSCALL_FSYNC; break;
 case SYS_sync: s=SYSCALL_SYNC; break;
 case SYS_munmap: s=SYSCALL_MUNMAP; break;
 case SYS_mprotect: s=SYSCALL_MPROTECT; break;
 case SYS_mmap:
  if(a || d!=(MAP_PRIVATE|MAP_ANONYMOUS) || e!=-1 || f) return -ENOTSUP;
  return call(SYSCALL_MMAP,b,c,0,0,0,0);
 case SYS_brk: return -ENOMEM; /* musl falls back to anonymous mappings. */
 case SYS_arch_prctl:
  if(a!=0x1002) return -EINVAL;
  return call(SYSCALL_SET_FS_BASE,b,0,0,0,0,0);
 case SYS_set_tid_address:
  /* There is no CLONE/thread exit support yet; single-thread startup only. */
  return call(SYSCALL_GETPID,0,0,0,0,0,0);
 case SYS_clock_gettime: s=SYSCALL_CLOCK_GETTIME; break;
 case SYS_nanosleep: s=SYSCALL_NANOSLEEP; break;
 case SYS_chdir: s=SYSCALL_CHDIR; break;
 case SYS_getcwd: {
  long r=call(SYSCALL_GETCWD,a,b,0,0,0,0);
  return r<0 ? r : (long)strlen((const char *)a)+1;
 }
 case SYS_execve: s=SYSCALL_EXECVE; break;
 default: return __os_network(n,a,b,c,d,e,f);
 }
 return call(s,a,b,c,d,e,f);
}
