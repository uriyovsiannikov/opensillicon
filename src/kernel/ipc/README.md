Inter-process communication code lets processes exchange data through kernel-managed endpoints. The available mechanisms include buffered pipes and local Unix-domain sockets.

Unix pathname sockets are backed by socket inodes in RAMFS or SilliconFS.
Binding requires write and search access to the parent directory and creates
the inode with mode `0777 & ~umask`. Connecting checks directory search access
and write access to the socket inode. `chmod`, `chown`, `stat`, directory
listing and sticky-directory `unlink` use the normal VFS rules. Closing a
socket leaves its pathname in place until `unlink`; unlinking a live socket
prevents new connections through that inode without breaking existing ones.
Abstract addresses (a leading NUL byte) remain a separate byte-string namespace
without filesystem permissions; services can authenticate peers with
`SO_PEERCRED`.

Run `sh src/tests/unix_peercred.sh` for host-side RAMFS and credential tests,
or `sh src/tests/permissions_guest.sh` for syscall tests in QEMU followed by
validation of persisted SilliconFS socket inodes. The QEMU test uses a separate
disk under `src/build/permissions-guest/`.

Pipes synchronize their ring buffer, readiness and endpoint lifetime with a
per-pipe IRQ-safe spinlock. Read copying and consumption form one transaction:
an invalid user destination leaves data queued even when readers run on
separate CPUs. The copy callback must not sleep or allocate. Scalar writes
and complete writev requests up to the 4096-byte capacity wait for enough
space and commit as one unit, so concurrent writers cannot mix small records.

Descriptor tables belong to one running process; fork, exec and exit transfer
ownership through the BSP. Cloned entries share atomic file references and
nonblocking status flags. Ready pipe reads/writes, vector I/O, creation and
close can execute on an AP. Empty reads and full writes that need to sleep
publish a local-wait request and release the AP. The BSP registers the wait
only after the AP has released the process stack and address space. On wake,
the saved syscall can resume on an AP, without replaying a user interrupt or
requiring I/O execution on the BSP. The BSP can execute the same continuation
when no AP is available.

AP notifications set a coalesced wake flag and send a BSP IPI. The BSP drains
it at user preemption and scheduler safe points, retrying channel waiters.
The notification does not retain pipe or channel pointers: final close can
free an object while a notification is in flight. Notifications arriving
between an unsuccessful I/O attempt and blocking are also detected by a
monotonic readiness generation, sampled before the attempt. If that generation
changed before wait publication, the process retries instead of sleeping.
This covers notifications already drained before the BSP collects an AP's
wait request. Unrelated channel waiters may retry
and block again. The periodic scheduler tick handles failed IPI delivery.

`sh src/tests/smp_user.sh pipes` exercises scalar and vectored pipe I/O,
concurrent readers and writers, small-record integrity, copy faults, EAGAIN,
EOF, EPIPE and final endpoint closes on 1, 2 and 8 CPUs. It waits for scalar
and vector readers/writers to become blocked, then tests EINTR and SIGKILL.
Multi-CPU runs must also prove AP wait publication, AP syscall resumption and
notifications that woke blocked channel waiters.
