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
replacing an existing layout is not a power-loss-atomic operation. After writing, gpart requests a root-only GPT reread and atomically refreshes
the disk nodes. Newly created partitions can be formatted in the same session.
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

`installboot [-N | -F] -r <root-PARTUUID> /dev/ada0p1` installs a UEFI boot
partition from those packaged payloads. Preview is the default; -F authorizes
formatting the target partition as FAT32 and writing boot files. The command
requires a canonical nonzero root UUID and a SATA partition path. Source file
types/signatures and target capacity are checked before writing. The generated
Limine configuration points at the selected GPT root UUID and does not include
a Live RAM root module. The firmware fallback path /EFI/BOOT/BOOTX64.EFI avoids
a requirement for NVRAM boot entry creation; BIOS boot is not supported.

`python3 src/tests/installboot_qemu.py` prepares a temporary root filesystem,
installs boot files by running the actual command in a Live guest, checks the
ESP with fsck.fat/mcopy, then boots without any ISO twice. A file created on the
first disk-only boot must survive the second. This test validates boot setup;
its root files are prepopulated on the host, so it does not claim to test the
full installer copy stage. The complete blank-disk installation test is
`python3 src/tests/scinstall_qemu.py`; it also compares persisted system
contents, ownership and modes with the Live image. See INSTALL.md for usage.

Temporary target filesystems can now be mounted in the Live session:
mount -t silliconfs /dev/ada0p2 /mnt; then use umount /mnt after closing files.
Both commands require root. sync reports errors flushing mounted devices;
fsync on the target root directory flushes the backing SilliconFS device.

The install-copy component scans source directories breadth-first without
holding a descriptor per recursion level, computes content CRCs and conservative
space requirements, and detects unsupported objects/oversized paths before disk
changes. A second scan can revalidate the inventory before destructive stages.
Copy verifies source metadata/content against that inventory, writes new files
with exclusive creation and preserves UID/GID and all supported mode bits.
Directory ownership and modes are applied after copying, children before
parents. A single target filesystem flush precedes the separate readback pass
that checks destination content and metadata. SilliconFS currently has no symlinks, hard links
or persistent timestamps; these are not synthesized by the copier.

/dev, /proc, /init, /mnt and /var/run are copied as empty directories. /tmp keeps
only the packaged img1.jpg background. This avoids copying devices, the mounted
initramfs, installation targets, control sockets and transient logs.

sh src/tests/install_copy.sh checks content/permissions, runtime exclusions,
source changes and rejection of host symlinks. python3 src/tests/copy_qemu.py
checks the real libos/VFS path, indirect-block files, preserved UID/GID and mode
bits, then unmounts/remounts the target to verify the persisted results.
