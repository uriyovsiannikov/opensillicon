# musl migration — static runtime and files

This is an initial static x86-64 port of musl 1.2.5 to OpenSillicon ABI v7.
musl is now the sole libc in the build graph; the former native libc sources
and their implementation-specific tests have been removed. Existing binaries,
libraries and packages from the old runtime must be rebuilt. The OS syscall
adapter is still incomplete: switching the build does not establish full system
or package runtime compatibility. Signals and sockets are partially supported; threads still need implementation.
Init now uses musl CRT startup and TLS; the runtime
probe checks TLS rather than replacing the thread pointer inside init.

## Build and check

From the repository root:

```
make musl
make musl-check
make musl-system-check
```

The second target builds kernel.elf, initramfs.cpio, populate_root, shell.elf
and the e1000 module, and requires QEMU/KVM, OVMF and xorriso. It boots a separate
ISO and root image under `src/build/musl-check`, running the probe as init.
It verifies CRT startup, initialized TLS, constructors, malloc/realloc, errno,
stdio, clock_gettime and ENOSYS for unsupported syscall numbers.
The filesystem probe checks file creation/exclusive open, CLOEXEC/nonblocking
flags, dup/shared offsets, stat/lstat/fstat/fstatat, chmod/access, buffered 10 KB
stdio IO, directory enumeration/rewind, cwd, unlink/rmdir, and rejection of
unsupported flags without creating a file. It also checks fork/TLS isolation,
pipe communication, wait status encoding/size, EFAULT without losing a child,
signal termination, exec arguments/environment,
signal handler return, mask deferral/restoration and ignored signals.
Network checks include UNIX stream bind/listen/connect/accept4, data exchange,
CLOEXEC/nonblocking flags, duplicated descriptors, select readiness and EOF.
The harness supplies e1000, lets the kernel autoload it and obtain DHCP, and
starts local TCP/UDP echo servers. Guest IPv4 traffic reaches those servers
through QEMU user networking; tests need no external internet service.
TCP connect/send/recv/getpeername and UDP bind/getsockname/sendto/recvfrom,
scatter/gather messages, truncation and nonblocking EAGAIN are checked.
Poll checks cover ready data, timeouts, negative/invalid descriptors, HUP with
buffered data and events=0, EFAULT, and interruption by a signal. The DNS check
runs an ordinary forked UDP server inside the guest, resolves musl-port.test
through getaddrinfo, and verifies resolv.conf override and DHCP fallback. No
external DNS is used.
The kernel's halt diagnostic after init exits with status 0 is expected.
The probe permits argc=0 because the kernel starts init without arguments.

`make musl-system-check` builds and boots the actual Live ISO with the normal
musl init and user console. It drives the shell through QMP, checks `svctl list`,
`host localhost`, the package manager's usage output and authorized
`doas /bin/pkg` execution, then terminates QEMU.
Logs and a console screenshot are under `src/build/musl-system-check/`.
This is a base-system startup check; it does not establish installer or desktop
application compatibility. ncurses dependencies are configured and rebuilt
when the compiler flags or libc archive change, rather than reusing old objects.

Compile static programs using `src/build/musl/sysroot/bin/musl-gcc`,
`-static -no-pie -mno-red-zone -Wl,-T,src/user/thirdparty/musl/opensillicon/linker.ld`.
The linker script includes constructor arrays and TLS segments.

`build.sh` copies pristine upstream source into the build directory and applies
local overlays. No download or modification of vendored source occurs at build
time. Upstream source: https://musl.libc.org/releases/musl-1.2.5.tar.gz
SHA-256: a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
Upstream license is in `../COPYRIGHT`.

## ABI boundary and remaining work

All generic syscall paths go through `__os_syscall`; x86-64 assembly paths are
also replaced. Linux syscall numbers must never be sent directly to the kernel.
The raw adapter uses int 0x80 and converts carry/error results to negative errno.
Kernel ENOSYS 78 is translated to musl ENOSYS 38.

Currently supported: basic descriptor IO, seek, anonymous private mmap,
munmap/mprotect, process IDs, TLS initialization, clocks, chdir/getcwd, execve;
open/openat, fcntl descriptor/status flags, dup/dup2, stat/lstat/fstat/
fstatat, getdents64, mkdir/unlink/rmdir, chmod/fchmod/access and umask.
The kernel stat and directory layouts match musl x86-64; compile-time checks
protect their sizes and key offsets. getcwd converts the native zero success
result to the Linux length result expected by musl.

Open supports access mode, CREAT/EXCL/TRUNC/DIRECTORY/NONBLOCK/APPEND,
CLOEXEC, NOFOLLOW (the VFS has no symlinks), NOCTTY (there is no implicit
controlling-terminal assignment), and LARGEFILE (native 64-bit offsets).
Native OPENAT resolves relative paths from the held directory vnode, preserving
the directory capability across cwd changes and avoiding permission checks of
ancestors above that vnode. Nested directory descriptors also work. Absolute
paths ignore dirfd. Creation under unlinked directories is rejected in ramfs
and SilliconFS, including after another directory reuses the same pathname.
Directories opened through a vnode capability have no cached absolute path;
legacy internal pathname queries report ENOENT rather than an invented path.
Descriptor-based chdir is still unimplemented.

CLOEXEC is set inside the native open syscall. Append selects the current EOF
for each regular-file write; all such writes execute serially on the BSP.
F_SETFL can set/clear append and nonblocking status flags, shared through dup.
This relies on BSP serialization and will need a file/inode lock if regular
file writes are later allowed to execute concurrently on APs. The tests include
two independently opened append writers, stdio append, offset/status checks,
CLOEXEC across exec, capability permissions, and removed/recreated directories.
Unsupported flags such as O_SYNC fail before creation/truncation side effects.
fcntl supports GETFD/SETFD/GETFL/SETFL, F_DUPFD and F_DUPFD_CLOEXEC. Native
duplication chooses the lowest free descriptor at or above the requested
minimum and sets descriptor flags before returning. dup3 validates flags and
identical descriptors before replacing its destination, with CLOEXEC set in
the same syscall. Duplicates share offsets/status flags but have independent
descriptor flags. Locking and ownership commands remain unsupported.
Native pipe2 sets CLOEXEC/NONBLOCK on both ends before returning. Open and
paired-descriptor allocation now reuse closed standard descriptors 0–2.
Tests check duplicate offsets/flags, invalid flags and descriptors, minimum
bounds, nonblocking pipe access modes, exhaustion/EMFILE without consuming
output slots, and reuse of closed standard descriptors in a child process.
Terminal ioctl translates the native termios structure (which lacks Linux's
c_line field) to musl's public structure. TCSANOW/TCSADRAIN/TCSAFLUSH,
tcdrain/tcflush, winsize get/set, raw mode, isatty and FIONREAD are tested on a
PTY. Writes reach the console or PTY peer receive queue synchronously, so
there is no outstanding transmit queue to drain. Flush drops unread input,
including canonical partial lines, without discarding data already delivered
to a peer. PTYs retain attribute values and apply the configured erase/EOF
characters. Linux PTY allocation via /dev/ptmx still requires work; the test
uses native PTY_PAIR. A real ncurses screen is also tested through newterm,
cbreak/noecho, refresh and endwin.
The adapter reports EINVAL for readlink on existing non-symlink nodes, while
preserving native lookup and permission errors. This lets musl realpath work
with the VFS, which currently has no symlinks. Canonicalization and missing/
non-directory path errors are checked, including absolute command paths in doas.
Read/write requests are capped at the kernel's 4096-byte staging limit.
Process support includes fork, execve (up to the kernel limit of 16 arguments),
waitpid via native WAITPID, kill/raise, pipe, UID/GID queries and
setuid/setgid/setresuid/setresgid. WAITPID writes an encoded 32-bit POSIX status. Normal exits such
as exit(143) remain distinct from SIGTERM termination. wait4 with a non-null
rusage pointer is not implemented; neither are process groups or job control.

Sigaction supports handler masks and SA_RESETHAND, as well as SIG_DFL/SIG_IGN.
The native syscall carries handler/restorer/mask/flags in one structure. Linux/native mask bit numbering is translated explicitly.
The kernel applies the action mask and blocks the delivered signal, then
restores the previous mask through the native int 0x80 restorer. SIGKILL and
SIGSTOP are excluded from masks. Installing a SIGKILL action through the
native ABI is rejected. SA_RESETHAND resets the disposition before entering
the handler. Tests cover mask queries and deferred delivery, restoration,
native ABI output size and repeat delivery of a one-shot signal.
SA_RESTART, SA_SIGINFO, alternate stacks and nested handlers remain incomplete;
unsupported handler flags return EOPNOTSUPP.
Socket support includes UNIX streams, IPv4 TCP and UDP. A native descriptor
kind query avoids user-space descriptor caches that would become stale across
dup, fork or exec. IPv4 sockaddr/port values are translated to native endpoint
structures. socket/accept4 CLOEXEC and NONBLOCK use fcntl after creation;
atomic flag installation is still required before enabling threads.
Basic bind/connect/listen/accept/shutdown, getsockopt/setsockopt, IPv4 name
queries, sendto/recvfrom, sendmsg/recvmsg, select and poll are available. Stream IO is capped at 4096
bytes per syscall; UDP datagrams at 512 bytes. A short UDP receive buffer
truncates the datagram. Unsupported message flags return EOPNOTSUPP.
Message IO supports up to 16 iovecs, with no ancillary-data transfer.
UDP send accepts MSG_NOSIGNAL (datagrams cannot raise SIGPIPE); recvmsg reports
MSG_TRUNC and supports requesting the full datagram size on truncation.
IPv6, socketpair, UNIX name queries, ppoll and many socket options remain
incomplete, as does stream MSG_NOSIGNAL support needed for DNS TCP fallback.

Native poll accepts up to 1024 entries and uses scheduler/channel waits.
FD_POLL_HANGUP reports closed pipe writers and closed stream peers separately
from errors. UDP readiness is based on its actual receive queue; UDP recvfrom
also sleeps in the scheduler instead of retaining the BSP in a polling loop.
Datagrams addressed to this interface are delivered locally to bound sockets.
The musl resolver honors /etc/resolv.conf and falls back to the DHCP DNS server
when it has no nameserver. New root images no longer hardcode a nameserver.

Threads, vfork and other unmapped calls are unsupported, with ENOSYS.
The thread ID startup hook supports the initial thread only; clone is disabled.
Shared libc and the dynamic loader are not built.

Remaining migration stages:

1. Complete fcntl locking, descriptor-based chdir, remaining termios
   operations and Linux PTY allocation.
2. Complete remaining signal flags, nested/alternate frames, resource accounting, process groups and
   ppoll/pselect.
3. Complete socket flags/options, ancillary messages, UNIX name queries and
   DNS TCP fallback.
4. Implement kernel thread creation, futexes and thread teardown before enabling
   pthread creation; test cancellation and TLS isolation.
5. Move system programs and package builds onto the musl sysroot, rebuild every
   dependent library and verify boot/install/network/desktop.

## Native ABI cleanup

Only EXECVE, WAITPID with a 32-bit POSIX status, and full-structure SIGACTION
remain. EXECVE accepts path/argv/envp directly, copying both vectors through
one kernel helper. The old buffer/path/argv exec syscalls, byte-status wait and two-pointer
signal action ABI have been removed. All base programs and ports must be rebuilt.
Syscall assignments live in `src/include/abi/syscall_list.h`; retired slots are
not reused. The musl adapter never falls back to older kernel ABIs.

ABI v4 removes the flagless native PIPE call. Both libc pipe and pipe2
use PIPE2; pipe supplies zero flags. Slot 23 remains retired.

ABI v5 removes native DUP and DUP2 (slots 58 and 53). libc dup uses fcntl
F_DUPFD with minimum zero; dup2 uses DUP3 with zero flags and handles the
identical-descriptor query through F_GETFD. DUP3 installs descriptor flags as
part of the replacement, using one fd_dup_to helper.

ABI v6 removes native OPEN (slot 10). Native utilities and libc use OPENAT
with OS_AT_FDCWD for ordinary path opens. The kernel no longer decodes two
argument layouts for the same operation.

ABI v7 removes the compact native STAT call (slot 14). Native utilities and
libc share the full metadata contract in `abi/stat.h` through FSTAT/FSTATAT.
The unused VFS path-stat wrappers and compact kernel/userspace structures are
removed; file types are encoded in st_mode.
