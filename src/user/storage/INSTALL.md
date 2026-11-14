# Installing OpenSillicon

Build the self-contained image with `make -j8 live-iso`, then boot
`src/build/opensillicon-live.iso` using UEFI. Select **OpenSillicon Installer**
in the Limine menu. The normal Live entry starts the usual user console.
Only the Installer entry starts `scinstall` as root, and only with a mounted
Live RAM root. Exiting or cancelling restores the normal user console.

The installer supports a complete SATA disk accessible through AHCI. It creates
GPT with a 512 MiB FAT32 EFI System Partition and a SilliconFS root partition
using the remaining aligned space. Root must have at least 64 MiB and enough
space and inodes for the Live system. A 1 GiB disk accommodates the current
system. It replaces the selected disk's existing partition table and contents.
Installing into existing partitions or alongside another OS is not supported.
NVMe, USB storage and BIOS boot are not supported by this installation path.

1. Use Up/Down to highlight a disk and Enter to review it. Check the capacity,
   model and serial number. The menu marks mounted disks as in use and keeps
   them unavailable for installation. Esc or Q exits the disk menu.
2. Review the partition plan and source inventory. The installer checks the
   boot payloads, available space, disk flush support and source files before
   asking for confirmation.
3. Enter the exact confirmation, such as `ERASE /dev/ada0`. Any other answer
   cancels without writing partition or filesystem data. Esc also cancels.
4. The status screen shows GPT creation, root formatting, separate copy and
   readback progress, boot setup, journal saving, unmount and final flush. Only
   `INSTALLATION COMPLETE` means all stages succeeded.
5. After installation, select **Reboot** or **Return to console** with
   Left/Right and Enter. Remove the Live USB before booting the installed disk.

The interactive interface uses the ncurses library shared with nano and htop.
`scinstall --text` uses line prompts. The installer also uses line prompts if
it cannot initialize the terminal interface.

`scinstall -N /dev/ada0` performs preflight and displays the plan without
partition or filesystem writes; it requires root in the Live environment.

The installer preserves file contents, UID/GID and supported permission bits.
It copies `/dev`, `/proc`, `/init`, `/mnt` and `/var/run` as empty directories;
`/tmp` keeps only the packaged background image. SilliconFS does not currently
store symlinks, hard links or persistent timestamps.

Limine is installed at the UEFI fallback path `/EFI/BOOT/BOOTX64.EFI`, so no
firmware NVRAM entry is required. Its configuration selects the new root by
GPT partition UUID. The installed root is on disk, without the Live RAM module.

During installation, diagnostics and child-tool output are recorded in
`/tmp/scinstall.log` in RAM. A copy is saved at `/var/log/install.log` on the
target before final unmount. The installed copy records file and boot setup
verification; final unmount/flush results remain in the Live journal. On error,
the installer reports the failed stage and errno, attempts to unmount the target,
and returns to the user console. Installation does not roll back an erased disk
and is not atomic across power loss. Do not boot a partially installed target;
restart installation from the Live image.

# Verification

Run `python3 src/tests/scinstall_qemu.py` from the repository root. It uses only
temporary disk images: cancellation must leave both disks unchanged; installation
must populate a blank disk while leaving a second disk unchanged. Independent
GPT/FAT checks verify the boot payloads and root UUID, then two disk-only UEFI
boots check installed files and persistence. QEMU/KVM, OVMF, xorriso, sgdisk,
fsck.fat and mcopy are required. This is a virtual hardware test, not proof of
compatibility with every physical SATA controller or firmware.
