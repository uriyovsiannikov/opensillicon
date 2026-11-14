Storage tools run with root credentials and require a character disk device
implementing DIOCGSECTORSIZE and DIOCGMEDIASIZE. They do not accept ordinary
files as live targets. Disk I/O is bounded by the reported media size; transfers
use complete sectors and DIOCGFLUSH propagates errors to the caller.

`newfs_silliconfs -N [-i inodes] /dev/ada0p2` previews a version 1 SilliconFS.
Preview is the default. `-F` explicitly enables formatting. The default is 1024
inodes; the device size must be a multiple of 4096 bytes. Formatting invalidates
the old superblock, writes allocation maps, a cleared inode table and root
directory, flushes metadata, then publishes and reads back the new superblock.
Free data blocks are not erased. Any failed write or flush aborts the operation.
The kernel rejects raw writes to mounted devices or their overlapping parents.

Host regression: `sh src/tests/newfs_image.sh` compares byte-for-byte with the
independent Python formatter and injects write/flush failures. Native regression:
`python3 src/tests/newfs_qemu.py` builds a temporary Live test image, runs the
real utility in QEMU, checks preview leaves data untouched, checks mounted-root
protection, and compares the resulting disk image with the Python formatter.

`gpart bootlayout [-N | -F] [-e esp-MiB] /dev/ada0` prepares a GPT with an EFI
System Partition and a SilliconFS partition. The default ESP is 256 MiB; the
minimum supported ESP and root sizes are each 64 MiB. Partition starts are
aligned to 1 MiB and the root size is a multiple of 4096. Preview is the default;
-F authorizes replacing both existing GPT copies and the protective MBR.
The tool accepts only whole SATA device names, obtains distinct UUIDs from
/dev/urandom, and aborts without writes if randomness is unavailable.

The backup GPT is flushed before the primary, both copies are read back, then
the protective MBR is written, flushed and verified. Failures stop the operation;
replacing an existing layout is not a power-loss-atomic operation. Partition
device nodes are currently discovered at boot, so reboot after using this tool.
Neither FAT32 nor SilliconFS is formatted by gpart. The project's SilliconFS GPT
type is 53494c46-5300-4000-8000-4f50454e5349.

`sh src/tests/gpt_write.sh` checks 512/4096-byte sectors, injected failures,
readback mismatches and independent sgdisk validation. Native end-to-end check:
`python3 src/tests/gpart_qemu.py` creates GPT in a temporary guest disk, verifies
it with sgdisk, reboots, and formats the discovered root partition with newfs.
