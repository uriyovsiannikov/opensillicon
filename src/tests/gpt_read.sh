#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Isrc/include src/tests/gpt_read.c \
    src/kernel/fs/block/gpt.c src/kernel/fs/block/block.c -o "$test_binary"
python3 src/tests/gpt_read.py "$test_binary"
