#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
src="$root/src/tools/pkg/repo/_src/xload/upstream"
out="$root/src/build/x11/clients/xload-port"
stage="$root/src/build/pkg/xload-stage"
cc=${CC:-gcc}
make -C "$root" src/build/x11/.apps-ready
mkdir -p "$out" "$stage/usr/bin" "$stage/usr/share/licenses/xload" "$stage/usr/share/X11/app-defaults"
cflags="-std=gnu11 -O2 -ffreestanding -fno-builtin -fno-strict-aliasing -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -DOPEN_SILLICON -D_CONST_X_STRING -I$root/src/include -I$root/src/user/libos/include -I$root/src/build/x11/prefix/include -I$root/src/build/x11/libX11/include -I$src -isystem $($cc -print-file-name=include) -idirafter /usr/include"
$cc $cflags -DPACKAGE_STRING='"xload 1.1.4"' -c "$src/xload.c" -o "$out/xload.o"
$cc $cflags -I"$root/src/tools/pkg/repo/_src/xload" -c "$root/src/tools/pkg/repo/_src/xload/opensillicon_load.c" -o "$out/opensillicon_load.o"
$cc -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,"$root/src/user/c_linker.ld" \
	"$root/src/build/libos/crt0.o" "$out/xload.o" "$out/opensillicon_load.o" -L"$root/src/build/x11/prefix/lib" \
	-Wl,--start-group -lXaw7 -lXpm -lXmu -lXt -lSM -lICE -lXext -lX11 -lxcb -lXau \
	"$root/src/build/x11/prefix/lib/libi18n.a" "$root/src/build/libos/liblibos.a" -Wl,--end-group -lgcc -o "$out/xload"
install -m 755 "$out/xload" "$stage/usr/bin/xload"
install -m 644 "$src/COPYING" "$stage/usr/share/licenses/xload/COPYING"
install -m 644 "$src/app-defaults/XLoad" "$stage/usr/share/X11/app-defaults/XLoad"
