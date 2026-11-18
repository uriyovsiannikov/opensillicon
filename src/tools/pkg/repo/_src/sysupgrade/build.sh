#!/bin/sh
set -eu
stage=$1
mkdir -p "$stage/usr/bin"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
make -C "$root" src/build/coreutils/sysupgrade.elf
install -m 755 "$root/src/build/coreutils/sysupgrade.elf" "$stage/usr/bin/sysupgrade"
