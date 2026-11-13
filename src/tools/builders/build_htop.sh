#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${CC:-gcc}
source="$root/src/user/thirdparty/htop"
out="$root/src/build/htop"
curses="$root/src/build/nano/ncurses"
libos="$root/src/build/libos"

mkdir -p "$out"
cd "$source"
# Reuse the upstream list of common sources and its portable platform backend.
files=$(sed -n '/^myhtopsources = /,/^[^[:space:]]/p; /^unsupported_platform_sources = /,/^[^[:space:]]/p' Makefile.am |
    awk '$1 ~ /\.c$/ { print $1 }')
set --
for file in htop.c $files opensillicon/port.c; do
    case "$file" in
        unsupported/Platform.c) file=opensillicon/Platform.c ;;
        unsupported/UnsupportedMachine.c) file=opensillicon/Machine.c ;;
        unsupported/UnsupportedProcessTable.c) file=opensillicon/ProcessTable.c ;;
    esac
    object="$out/$(printf '%s' "${file%.c}" | tr / _).o"
    "$cc" -std=gnu11 -O2 -DNDEBUG -ffreestanding -fno-builtin -fno-stack-protector \
        -fno-pie -m64 -mno-red-zone -U__linux__ -nostdinc \
        -Iopensillicon -I. -Iunsupported -I"$root/src/include" \
        -I"$root/src/user/libos/include" -I"$curses/include" \
        -isystem "$("$cc" -print-file-name=include)" -c "$file" -o "$object"
    set -- "$@" "$object"
done
"$cc" -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
    -Wl,-T,"$root/src/user/c_linker.ld" "$libos/crt0.o" "$@" \
    -Wl,--start-group "$curses/lib/libncurses.a" "$libos/liblibos.a" \
    -Wl,--end-group -lgcc -o "$out/htop"
