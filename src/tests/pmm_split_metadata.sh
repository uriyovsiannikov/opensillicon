#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p src/build
test_bin=$(mktemp src/build/pmm-split.XXXXXX)
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -DKERNEL_SPINLOCK_HOST_TEST \
    -Isrc/include -Isrc/limine src/tests/pmm_split_metadata.c \
    src/kernel/mm/pmm.c src/kernel/core/boot/boot.c -o "$test_bin"
"$test_bin"
