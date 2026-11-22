#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
src="$root/src/tools/pkg/repo/_src/xlogo/upstream"
out="$root/src/build/x11/clients/xlogo-port"
stage="$root/src/build/pkg/xlogo-stage"
cc=${CC:-gcc}
make -C "$root" src/build/x11/.apps-ready
mkdir -p "$out" "$stage/usr/bin" "$stage/usr/share/licenses/xlogo" "$stage/usr/share/X11/app-defaults"
cflags='-std=c11 -D_DEFAULT_SOURCE -O2 -ffreestanding -fno-builtin -fno-strict-aliasing -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -DOPEN_SILLICON -D_CONST_X_STRING'
includes="-I$root/src/build/musl/sysroot/include -I$root/src/build/x11/prefix/include -I$root/src/build/x11/libX11/include -I$src -isystem $($cc -print-file-name=include) -idirafter /usr/include"
for file in Logo xlogo; do
	$cc $cflags $includes -DPACKAGE_STRING='"xlogo 1.0.7"' -c "$src/$file.c" -o "$out/$file.o"
done
$cc -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,"$root/src/user/c_linker.ld" \
	"$root/src/build/musl/sysroot/lib/crt1.o" "$out/Logo.o" "$out/xlogo.o" -L"$root/src/build/x11/prefix/lib" \
	-Wl,--start-group -lXaw7 -lXpm -lXmu -lXt -lSM -lICE -lXext -lX11 -lxcb -lXau \
	"$root/src/build/x11/prefix/lib/libi18n.a" "$root/src/build/musl/sysroot/lib/libc.a" -Wl,--end-group -lgcc -o "$out/xlogo"
install -m 755 "$out/xlogo" "$stage/usr/bin/xlogo"
install -m 644 "$src/COPYING" "$stage/usr/share/licenses/xlogo/COPYING"
install -m 644 "$src/app-defaults/XLogo" "$stage/usr/share/X11/app-defaults/XLogo"
