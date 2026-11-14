The block layer provides read and write access to block devices for filesystem code. This directory also connects SilliconFS operations to block-backed storage.

AHCI disk discovery runs during PCI enumeration before selecting the root
filesystem. It scans SATA ports on up to eight AHCI controllers and registers
up to seven disks as ada0 through ada6, each with independent command and DMA
storage. One registry slot remains available for the Live RAM disk ram0.

Live boot mounts ram0 and exposes detected SATA disks through /proc/disks and
blkconf without mounting or formatting them. Disk boot selects the first detected
disk or GPT partition containing a valid SilliconFS. Device numbering follows PCI and
port discovery order; it is not a persistent identity. Legacy MBR partition discovery, NVMe,
and USB mass-storage block I/O are not implemented here.

GPT discovery validates protective MBR, header/entry-array CRC32, disk bounds,
unique partition IDs and non-overlapping ranges. A valid backup is used when
the primary is damaged; conflicting valid copies are rejected. Discovery never
repairs or writes metadata. Up to 128 entries of 128..1024 bytes are parsed;
the registry holds 128 total devices, reserving one slot for ram0. Partitions
are exposed as /dev/ada0p1, /dev/ada0p2, etc., using their original GPT indexes.
Raw writes through a parent disk are blocked if a child is mounted, and raw
writes through a child are blocked if its parent is mounted.

Run `sh src/tests/block_partitions.sh` and `sh src/tests/gpt_read.sh` for host
checks. `python3 src/tests/gpt_qemu.py` checks booting a SilliconFS in GPT
partition 3, alongside other disks; it requires the built ISO/root image,
sgdisk, QEMU with KVM, and OVMF. All test disk writes target temporary images.

Installed disk boots may specify `root=PARTUUID=<canonical UUID>` in the kernel
command line. The selector uses GPT UUID byte order, accepts upper/lowercase
hex digits, and requires exactly one matching partition. Invalid selectors,
missing targets and duplicate UUIDs halt boot instead of choosing a different
filesystem. Without a root selector, existing development images retain the
first-valid-SilliconFS behavior; a Live root module remains authoritative.
`sh src/tests/root_device.sh` checks parsing/uniqueness and
`python3 src/tests/root_uuid_qemu.py` boots the selected partition while a
different valid filesystem is discovered earlier.
