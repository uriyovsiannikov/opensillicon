#!/bin/sh
# Exercise the actual native libc implementation with the host runtime.
set -eu
cd "$(dirname "$0")/../../.."
check_dir=$(mktemp -d)
trap 'rm -rf "$check_dir"' EXIT HUP INT TERM
cc=${CC:-gcc}
"$cc" -std=c11 -g -O1 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
    -nostdinc -Isrc/include -Isrc/user/libos/include \
    -isystem "$("$cc" -print-file-name=include)" \
    -c src/user/libos/src/io/stdio.c -o "$check_dir/stdio.o"
# Keep native FILE objects and entry points separate from the host libc.
nm -g --defined-only "$check_dir/stdio.o" | awk '{print $3, "audit_" $3}' > "$check_dir/symbols"
printf "errno audit_errno\nread audit_read\nwrite audit_write\n" >> "$check_dir/symbols"
objcopy --redefine-syms="$check_dir/symbols" "$check_dir/stdio.o"
"$cc" -std=c11 -Wall -Wextra -Werror -Wl,--gc-sections \
    src/tools/checks/libos_stdio.c "$check_dir/stdio.o" -o "$check_dir/check"
"$check_dir/check"
