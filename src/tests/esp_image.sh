#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
make -j8 kernel src/build/initramfs.cpio
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror src/tests/esp_image.c \
 src/user/storage/esp.c src/user/storage/encoding.c -o "$binary"
python3 src/tests/esp_image.py "$binary"
