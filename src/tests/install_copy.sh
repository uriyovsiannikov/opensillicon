#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
 src/tests/install_copy.c src/user/storage/install_copy.c src/user/storage/encoding.c -o "$binary"
python3 src/tests/install_copy.py "$binary"
