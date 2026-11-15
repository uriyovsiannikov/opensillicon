#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc=${CC:-cc}
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -fsanitize=undefined -fsanitize-undefined-trap-on-error \
    -Dmalloc=os_malloc -Dcalloc=os_calloc -Drealloc=os_realloc -Dfree=os_free \
    -c src/user/libos/src/allocator.c -o "$build/allocator.o"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror \
    -fsanitize=undefined -fsanitize-undefined-trap-on-error \
    src/tests/libos_allocator.c "$build/allocator.o" -o "$build/test"
"$build/test"
