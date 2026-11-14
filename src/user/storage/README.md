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
System Partition and a SilliconFS partition. The default ESP is 512 MiB. The minimum supported ESP is 64 MiB on 512-byte
sectors and 260 MiB on 4096-byte sectors, so its FAT32 cluster count is valid.
The minimum root size is 64 MiB. Partition starts are
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

ESP formatting uses FAT32 with two allocation tables, FSInfo and backup boot
records. It writes /EFI/BOOT/BOOTX64.EFI, /EFI/BOOT/limine.conf, /boot/kernel.elf
and /boot/init.cpi; Limine can read these files without SilliconFS support.
The writer uses 512- or 4096-byte logical sectors and one sector per cluster,
checks FAT32's cluster-count limits, verifies metadata and file payloads, and
publishes the primary boot record only after data has been flushed.
`sh src/tests/esp_image.sh` checks both sector sizes with fsck.fat and mcopy,
compares all boot files, and injects failed writes and flushes. This component
supports UEFI; it does not install BIOS boot code.

Root and Live images include installable boot payloads at /boot/BOOTX64.EFI,
/boot/kernel.elf and /boot/init.cpi. The packaged kernel removes debug sections
so it fits SilliconFS's current per-file limit; its entry and loadable contents
remain identical to the development ELF. The full debug ELF stays in src/build.
