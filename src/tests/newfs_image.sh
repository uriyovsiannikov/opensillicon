#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror src/tests/newfs_image.c \
    src/user/storage/newfs.c src/user/storage/encoding.c -o "$test_binary"
python3 src/tests/newfs_image.py "$test_binary"
