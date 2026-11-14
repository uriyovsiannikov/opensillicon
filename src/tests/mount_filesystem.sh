#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
directory=$(mktemp -d)
trap 'rm -rf "$directory"' EXIT HUP INT TERM
python3 src/tools/packers/mksilliconfs.py "$directory/disk.img" --blocks 128 --inodes 128
cp "$directory/disk.img" "$directory/second.img"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
 -Isrc/include -Isrc/limine src/tests/mount_filesystem.c \
 src/kernel/fs/block/block.c src/kernel/fs/block/block_silliconfs.c src/kernel/fs/block/mount.c \
 src/kernel/fs/vfs/vfs.c src/kernel/fs/ramfs/ramfs.c src/kernel/fs/silliconfs/silliconfs.c \
 src/kernel/proc/fd.c -Wl,--gc-sections -o "$directory/test"
"$directory/test" "$directory/disk.img" "$directory/second.img"
