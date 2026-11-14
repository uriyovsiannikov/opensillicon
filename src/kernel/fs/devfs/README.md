devfs exposes `/dev/console`, `/dev/tty`, `/dev/null`, `/dev/zero`,
`/dev/random`, and `/dev/urandom` through the common filesystem interface.
`/dev/tty` currently aliases the single system TTY. Both random devices
currently use the bounded kernel RDRAND provider and return `EAGAIN` when it
cannot provide bytes. They have identical behavior and do not yet provide a
cryptographic random stream or blocking entropy-pool semantics.

Detected SATA disks are exposed as FreeBSD-style character device nodes
`/dev/ada0` through `/dev/ada6`; Live boot also exposes `/dev/ram0`.
These names match the block registry and blkconf. Nodes are owned by root:root
with mode 0600. Their stat size is the media size in bytes.

Disk descriptors support SEEK_SET, SEEK_CUR and SEEK_END. Raw reads and writes
require sector-aligned offsets and lengths (512 or 4096 bytes, as reported by
blkconf); user buffers do not need alignment. Reads at end of media return EOF,
writes at or beyond the end return ENOSPC, and requests crossing the end return
a short transfer. Raw writes to a disk hosting the mounted root filesystem
return EBUSY, including ram0. Filesystem I/O continues through the block layer.
Other character devices remain non-seekable. Disk nodes are populated when
devfs mounts; partition nodes, hotplug updates and BSD disk ioctls are not yet
implemented.

Run `sh src/tests/devfs_disks.sh` for the disk node and permission regression
checks, including 512/4096-byte sector I/O and mounted-disk protection.
