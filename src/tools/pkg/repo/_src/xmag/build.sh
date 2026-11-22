#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
src="$root/src/tools/pkg/repo/_src/xmag/upstream"
out="$root/src/build/x11/clients/xmag-port"
stage="$root/src/build/pkg/xmag-stage"
cc=${CC:-gcc}
make -C "$root" src/build/x11/.apps-ready
mkdir -p "$out" "$stage/usr/bin" "$stage/usr/share/licenses/xmag" "$stage/usr/share/X11/app-defaults"
cflags='-std=c11 -O2 -ffreestanding -fno-builtin -fno-strict-aliasing -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -DOPEN_SILLICON -D_CONST_X_STRING'
includes="-I$root/src/build/musl/sysroot/include -I$root/src/build/x11/prefix/include -I$root/src/build/x11/libX11/include -I$src -isystem $($cc -print-file-name=include) -idirafter /usr/include"
for file in CutPaste RootWin Scale xmag; do
	$cc $cflags $includes -DPACKAGE_STRING='"xmag 1.0.8"' -c "$src/$file.c" -o "$out/$file.o"
done
$cc -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,"$root/src/user/c_linker.ld" \
	"$root/src/build/musl/sysroot/lib/crt1.o" "$out/CutPaste.o" "$out/RootWin.o" "$out/Scale.o" "$out/xmag.o" -L"$root/src/build/x11/prefix/lib" \
	-Wl,--start-group -lXaw7 -lXpm -lXmu -lXt -lSM -lICE -lXext -lX11 -lxcb -lXau \
	"$root/src/build/x11/prefix/lib/libi18n.a" "$root/src/build/musl/sysroot/lib/libc.a" -Wl,--end-group -lgcc -o "$out/xmag"
install -m 755 "$out/xmag" "$stage/usr/bin/xmag"
install -m 644 "$src/COPYING" "$stage/usr/share/licenses/xmag/COPYING"
install -m 644 "$src/app-defaults/Xmag" "$stage/usr/share/X11/app-defaults/Xmag"
