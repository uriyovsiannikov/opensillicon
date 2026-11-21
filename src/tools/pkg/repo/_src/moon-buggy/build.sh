#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
src="$root/src/tools/pkg/repo/_src/moon-buggy/upstream"
out="$root/src/build/moon-buggy"
stage="$root/src/build/pkg/moon-buggy-stage"
make -C "$root" src/build/nano/ncurses/lib/libncurses.a src/build/libos/crt0.o
cc=${CC:-gcc}
mkdir -p "$out" "$stage/usr/bin" "$stage/usr/share/licenses/moon-buggy"
sed -f "$src/text2c.sed" "$src/COPYING" > "$out/copying.h"
sed -n -f "$src/img.sed" "$src/car.img" > "$out/buggy.h"
cflags="-std=gnu11 -O2 -DNDEBUG -DOPEN_SILLICON -D_DEFAULT_SOURCE -D_POSIX_SAVED_IDS -DRETSIGTYPE=void -DHAVE_ERRNO_H=1 -DHAVE_SYS_SELECT_H=1 -DO_SHLOCK=0 -DO_EXLOCK=0 -DVERSION=\"1.1.0\" -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -m64 -mno-red-zone -nostdinc -I$root/src/include -I$root/src/user/libos/include -I$root/src/user/libos/src -I$root/src/build/nano/ncurses/include -I$out -I$src"
files='main mode title pager game level ground buggy laser meteor highscore realname queue vclock date persona signal keyboard terminal cursor random error xmalloc xstrdup hpath'
set --
for file in $files; do
	$cc $cflags -isystem "$($cc -print-file-name=include)" -DSCORE_DIR='"/tmp"' -c "$src/$file.c" -o "$out/$file.o"
	set -- "$@" "$out/$file.o"
done
$cc $cflags -isystem "$($cc -print-file-name=include)" -c "$root/src/tools/pkg/repo/_src/moon-buggy/opensillicon.c" -o "$out/opensillicon.o"
set -- "$@" "$out/opensillicon.o"
$cc -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,"$root/src/user/c_linker.ld" \
	"$root/src/build/libos/crt0.o" "$@" -Wl,--start-group "$root/src/build/nano/ncurses/lib/libncurses.a" \
	"$root/src/build/libos/liblibos.a" -Wl,--end-group -lgcc -o "$out/moon-buggy"
install -m 755 "$out/moon-buggy" "$stage/usr/bin/moon-buggy"
install -m 644 "$src/COPYING" "$stage/usr/share/licenses/moon-buggy/COPYING"
