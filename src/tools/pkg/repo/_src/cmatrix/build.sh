#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
src="$root/src/tools/pkg/repo/_src/cmatrix/upstream"
out="$root/src/build/cmatrix"
stage="$root/src/build/pkg/cmatrix-stage"
mkdir -p "$out" "$stage/usr/bin" "$stage/usr/share/licenses/cmatrix"
make -C "$root" src/build/nano/ncurses/lib/libncurses.a src/build/musl/sysroot/lib/crt1.o
cc=${CC:-gcc}
"$cc" -std=gnu11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -m64 -mno-red-zone -U__linux__ -DOPEN_SILLICON -DVERSION='"2.0"' -DEXCLUDE_CONFIG_H -DHAVE_NCURSES_H -DHAVE_UNISTD_H -DHAVE_TERMIOS_H -DHAVE_SYS_IOCTL_H -I"$root/src/build/nano/ncurses/include" -I"$root/src/build/musl/sysroot/include" -isystem "$("$cc" -print-file-name=include)" -c "$src/cmatrix.c" -o "$out/cmatrix.o"
"$cc" -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,"$root/src/user/c_linker.ld" "$root/src/build/musl/sysroot/lib/crt1.o" "$out/cmatrix.o" -Wl,--start-group "$root/src/build/nano/ncurses/lib/libncurses.a" "$root/src/build/musl/sysroot/lib/libc.a" -Wl,--end-group -lgcc -o "$out/cmatrix"
install -m 755 "$out/cmatrix" "$stage/usr/bin/cmatrix"
install -m 644 "$src/COPYING" "$stage/usr/share/licenses/cmatrix/COPYING"
