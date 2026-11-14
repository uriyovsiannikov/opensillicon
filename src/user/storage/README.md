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
