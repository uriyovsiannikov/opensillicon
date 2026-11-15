#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build=$(mktemp -d)
trap 'rm -f "$build/strftime.o" "$build/test"; rmdir "$build"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -O2 -fno-builtin -Wall -Wextra -Werror -Wno-nonnull-compare \
    -Dstrftime=os_strftime -Dctime=os_ctime \
    -c src/user/libos/src/strftime.c -o "$build/strftime.o"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    src/tests/libos_strftime.c "$build/strftime.o" -o "$build/test"
TZ=UTC0 "$build/test"
