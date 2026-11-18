#!/bin/sh
set -eu
stage=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
make -C "$root" src/build/kernel-boot.elf src/build/initramfs.cpio
mkdir -p "$stage/opt/os-kernel"
install -m 644 "$root/src/build/kernel-boot.elf" "$stage/opt/os-kernel/kernel.elf"
install -m 644 "$root/src/build/initramfs.cpio" "$stage/opt/os-kernel/init.cpi"
