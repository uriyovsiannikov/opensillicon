/* Translate musl's Linux syscall vocabulary to OpenSillicon ABI v6.
 * Never forward unknown numbers: the two number spaces overlap. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stddef.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stdint.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "abi/syscall.h"
#include "abi/credentials.h"
#include "abi/termios.h"
#include "abi/signal.h"
extern long __os_raw(long,long,long,long,long,long,long);
long __os_native_call(long n,long a,long b,long c,long d,long e,long f)
{
 long r=__os_raw(n,a,b,c,d,e,f);
 return r == -78 ? -ENOSYS : r;
}
#define call __os_native_call
long __os_network(long,long,long,long,long,long,long);

/* Kernel native_kstat and getdents64 deliberately use the x86-64 layout.
 * Fail the port build if musl changes these public ABI offsets. */
_Static_assert(sizeof(struct os_termios)==56, "termios ABI size");
_Static_assert(sizeof(struct stat)==144, "stat ABI size");
_Static_assert(offsetof(struct stat, st_mode)==24, "stat mode ABI");
_Static_assert(offsetof(struct stat, st_size)==48, "stat size ABI");
_Static_assert(offsetof(struct stat, st_atim)==72, "stat timestamps ABI");
_Static_assert(offsetof(struct dirent, d_name)==19, "getdents ABI");

static long open_native(long dirfd, long path, long flags, long mode)
{
 /* No symlinks exist in this VFS. O_NOFOLLOW is consequently satisfied.
  * Large file offsets are native. Never silently discard other flags. */
 const long allowed=O_ACCMODE|O_CREAT|O_EXCL|O_TRUNC|O_DIRECTORY|
     O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW|O_LARGEFILE|O_APPEND|O_NOCTTY;
 if(flags & ~allowed) return -EOPNOTSUPP;
 long native=flags & O_ACCMODE;
 if(flags & O_CREAT) native|=0x40;
 if(flags & O_EXCL) native|=0x80;
 if(flags & O_TRUNC) native|=0x200;
 if(flags & O_DIRECTORY) native|=0x10000;
 if(flags & O_NONBLOCK) native|=0x800;
 if(flags & O_APPEND) native|=0x400;
 if(flags & O_CLOEXEC) native|=0x80000;
 return call(SYSCALL_OPENAT,dirfd,path,native,mode,0,0);
}

static long fcntl_native(long fd,long cmd,long arg)
{
 switch(cmd) {
 case F_DUPFD: return call(SYSCALL_FCNTL,fd,0,arg,0,0,0);
 case F_DUPFD_CLOEXEC: return call(SYSCALL_FCNTL,fd,1030,arg,0,0,0);
 case F_GETFD: return call(SYSCALL_FCNTL,fd,1,0,0,0,0);
 case F_SETFD:
  if(arg & ~FD_CLOEXEC) return -EINVAL;
  return call(SYSCALL_FCNTL,fd,2,arg,0,0,0);
 case F_GETFL: {
  long r=call(SYSCALL_FCNTL,fd,3,0,0,0,0);
  if(r<0) return r;
  return (r & 3) | ((r & 0x800) ? O_NONBLOCK : 0) | ((r & 0x400) ? O_APPEND : 0);
 }
 case F_SETFL:
  if(arg & ~(O_ACCMODE|O_NONBLOCK|O_LARGEFILE|O_APPEND)) return -EOPNOTSUPP;
  return call(SYSCALL_FCNTL,fd,4,((arg & O_NONBLOCK) ? 0x800 : 0) | ((arg & O_APPEND) ? 0x400 : 0),0,0,0);
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
 case SYS_setresuid: s=SYSCALL_SETRESUID; break;
 case SYS_setresgid: s=SYSCALL_SETRESGID; break;
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
 case SYS_read: s=SYSCALL_READ; if(c>4096)c=4096; break;
 case SYS_write: s=SYSCALL_WRITE; if(c>4096)c=4096; break;
 case SYS_readv: s=SYSCALL_READV; break;
 case SYS_writev: s=SYSCALL_WRITEV; break;
 case SYS_open: return open_native(AT_FDCWD,a,b,c);
 case SYS_openat: return open_native(a,b,c,d);
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
  if(c>4096)c=4096;
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
