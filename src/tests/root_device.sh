#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Isrc/include src/tests/root_device.c \
 src/kernel/fs/block/root_device.c src/kernel/fs/block/block.c -o "$binary"
"$binary"
