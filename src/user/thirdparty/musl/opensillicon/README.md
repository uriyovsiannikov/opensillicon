# musl migration — static runtime and files

This is an initial static x86-64 port of musl 1.2.5 to OpenSillicon ABI v22.
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
Open-file contexts retain vnodes, without cached absolute paths or a legacy
pathname-query callback.
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
Scalar and vectored read/write use one kernel implementation and return short
results after at most 64 KiB per call. libc does not truncate application requests.
This lets libc finish large fread/fwrite calls without private application
chunking. Stream sendmsg/recvmsg use the same native vectored I/O;
UDP retains datagram gathering, scattering and truncation handling.
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
queries, sendto/recvfrom, sendmsg/recvmsg, select and poll are available.
Stream I/O shares the 64 KiB per-call kernel budget with file I/O; UDP
datagrams are limited to 512 bytes. A short UDP receive buffer
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

ABI v8 removes the private READDIR call (slot 17), its userspace wrapper and
its metadata-bearing directory structure. Applications use libc directory
streams over GETDENTS and query metadata through stat/fstatat as needed. The
internal VFS directory entry no longer carries unused size/mode fields.

## Native sysctl

The read-only SYSCTL call accepts a two-integer MIB, an optional output buffer
and its size pointer, and the standard replacement arguments (writes fail
with EPERM). libc exports sysctl, sysctlbyname and sysctlnametomib through
`sys/sysctl.h`. `abi/sysctl.h` is the canonical name/MIB/type registry.
Size queries include the string terminator. Short buffers receive a prefix,
report bytes copied and return ENOMEM. Unknown nodes return ENOENT.
The current tree contains kernel identity, machine/CPU/page information,
64-bit usable physical memory bytes and native `vm.freepages` (64-bit frames).
This provides BSD-style queries without parsing procfs or forwarding Linux
syscall numbers; it does not implement the complete FreeBSD sysctl tree.

ABI v9 removes PROCESS_SNAPSHOT (slot 9). ps and the xload adapter use the
shared PROCESS_INFO format with PID, parent PID, state, effective UID and name.
The duplicate process-list traversal and compact userspace structures are
removed. The internal current-process status used by procfs remains local to
the kernel and is not a syscall wire format.

ABI v10 removes SLEEP_TICKS (slot 8). All applications use nanosleep with
seconds/nanoseconds; the old tick-count syscall, userspace wrapper, public
timer-frequency constant and duplicate scheduler wait entry are removed.

ABI v11 replaces the separate TCP, UDP and Unix-domain bind/connect syscalls
with BIND and CONNECT. The kernel selects the protocol from the descriptor;
address validation and copying share one path. Slots 34, 44, 46, 63, 82 and 84
are retired and return ENOSYS. Rebuild static applications against ABI v11;
there are no aliases for the old calls.

ABI v12 replaces UDP_SOCKET, TCP_SOCKET and UNIX_SOCKET with SOCKET. The
kernel validates domain, type and protocol once, then allocates the selected
protocol socket. Slots 33, 43 and 81 return ENOSYS. Rebuild static applications
against ABI v12. Native family/type/protocol constants live in `abi/socket.h`.

ABI v13 replaces the four TCP/UDP address-query calls with GETSOCKNAME and
GETPEERNAME, also implemented for Unix sockets. Both take a descriptor,
address buffer and byte capacity, copy the available prefix and return the
full native address length. A zero capacity performs a size query. IPv4 uses
`os_ipv4_endpoint`; Unix sockets use `os_unix_address` from `abi/unix_socket.h`.
Slots 88, 89, 91 and 92 return ENOSYS. Rebuild static applications for ABI v13.
The libc Unix-socket ENOSYS substitute is removed; accepted sockets preserve
the listener's bound name and peer queries return the actual peer's name.

Unix-domain sockets use filesystem names, directory permissions and socket
vnodes. Linux abstract names (a leading NUL) are rejected with EINVAL; their
separate namespace and duplicate lookup code are removed. TinyX, XCB and
startx use `/tmp/.X11-unix/X0`. TinyX creates the socket directory, removes its
listener node when closing, and obtains accepted endpoints with the shared
getsockname/getpeername interface instead of copying the listener as its peer.

`flock` uses a native FLOCK syscall for advisory shared/exclusive locks on
regular files and directories. Ownership follows the open file description:
`dup` and `fork` share it, explicit unlock or the final close releases it.
Nonblocking conflicts return EAGAIN; blocking requests sleep and retry when a
lock is released. Lock conversion first releases the previous mode. Package
management uses this interface on its database directory, with no socket lock
namespace or separate target-specific implementation.

`kern.maxfilesperproc` reports the kernel descriptor-table capacity.
`getrlimit(RLIMIT_NOFILE)` and `sysconf(_SC_OPEN_MAX)` query this value.
Other resource limit queries are unsupported; limits are not currently mutable.

ABI v14 retires the dedicated DNS-server and IPv4-configuration syscalls
(slots 93 and 94). Native network queries use sysctl: `net.interface` is the
active device name, `net.inet.ip.config` is an `os_ipv4_config`, and
`net.inet.ip.dns` is four IPv4 octets. The latter two return EAGAIN while DHCP
configuration is unavailable. These are OpenSillicon MIB nodes, not a complete
BSD routing or interface tree. ifconfig and libc's resolver use this shared
interface. Rebuild static applications for ABI v14.
The unused `/proc/netif` export and the private userspace network wrappers
are removed. Static network configuration retrieval no longer requires procfs.

ABI v15 gives ICMP Echo one shared `os_icmp_echo_request` in `abi/icmp.h`.
The returned duration is nanoseconds, rather than scheduler ticks; the size
remains 56 bytes. Rebuild ping and other native ICMP callers for ABI v15.
The native `os_icmp_echo` libc extension in `sys/icmp.h` returns -1 and sets
errno on failure. ping uses libc address parsing and formatting and no longer
links the legacy userspace runtime. Its bounded count remains 1–4 requests.

ABI v16 removes the private TIOCSRAW/TIOCCANON ioctls (0x4f530001/2), which
now return ENOTTY. The shell uses tcgetattr/tcsetattr, saves the initial
terminal attributes and restores them for external programs and on exit.
Its editor disables input canonicalization and echo while preserving output
newline processing. Terminal dimensions use libc's winsize/TIOCGWINSZ.
The duplicate PTY raw-mode setter and legacy userspace ioctl wrapper are removed.

ABI v17 retires the debug kernel-halt syscall (slot 101) and removes the
hidden `!(crash)` shell command and its private runtime wrapper. Slot 101
returns ENOSYS. Shutdown and reboot continue through the power-control API.

ABI v19 removes the zero-stride framebuffer rectangle format. Every
`os_framebuffer_rect` source points to the rectangle's first pixel and supplies
an explicit `source_pitch` of at least `width * 4` bytes. Destination x/y select
only the framebuffer location. Rebuild native framebuffer clients and TinyX.

ABI v20 retires the unused full-framebuffer presentation syscall (slot 41).
All native framebuffer output uses `os_framebuffer_present_rect`; a full
screen is a rectangle at (0, 0) with the framebuffer dimensions and explicit
source stride. Slot 41 returns ENOSYS. Rebuild framebuffer clients.

ABI v21 replaces separate TCP/Unix listen syscalls (slots 83/45) with
LISTEN (117). The kernel selects the stream implementation from the file
object. Backlog must fit a nonnegative signed 32-bit integer for both types.
UDP returns EOPNOTSUPP; other files return ENOTSOCK. Rebuild static clients.

ABI v22 replaces TCP/Unix shutdown syscalls (slots 86/49) with SHUTDOWN
(118). The kernel validates the full-width mode before dispatch; values other
than OS_SHUT_RD, OS_SHUT_WR and OS_SHUT_RDWR return EINVAL for streams.
UDP returns EOPNOTSUPP; other files return ENOTSOCK. Rebuild static clients.

TCP shutdown disables the selected local direction immediately: reads return
EOF after SHUT_RD and writes fail with EPIPE after SHUT_WR, including through
duplicated descriptors. SHUT_WR preserves reception. The network service
flushes queued output before sending FIN, without waiting in the syscall for
peer acknowledgment; it also drains discarded input after SHUT_RD.

Partial TCP writes report the bytes already accepted by the transport even
when its remaining send buffer fills, so callers retry only the unsent suffix.
