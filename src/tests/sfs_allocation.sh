#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
directory=$(mktemp -d)
trap 'rm -rf "$directory"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
 -Isrc/include -Isrc/limine src/tests/sfs_allocation.c -Wl,--gc-sections -o "$directory/test"
"$directory/test"
