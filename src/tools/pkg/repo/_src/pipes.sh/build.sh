#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
out="$root/src/build/pipes.sh"
stage="$root/src/build/pkg/pipes.sh-stage"
mkdir -p "$out" "$stage/usr/bin" "$stage/usr/share/licenses/pipes.sh"
make -C "$root" src/build/nano/ncurses/lib/libncurses.a src/build/libos/crt0.o
cc=${CC:-gcc}
cflags="-std=c11 -O2 -D_DEFAULT_SOURCE -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -m64 -mno-red-zone -nostdinc -I$root/src/include -I$root/src/user/libos/include -I$root/src/build/nano/ncurses/include -isystem $($cc -print-file-name=include)"
$cc $cflags -c "$root/src/tools/pkg/repo/_src/pipes.sh/opensillicon.c" -o "$out/pipes.o"
$cc -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,"$root/src/user/c_linker.ld" \
	"$root/src/build/libos/crt0.o" "$out/pipes.o" -Wl,--start-group "$root/src/build/nano/ncurses/lib/libncurses.a" \
	"$root/src/build/libos/liblibos.a" -Wl,--end-group -lgcc -o "$out/pipes.sh"
install -m 755 "$out/pipes.sh" "$stage/usr/bin/pipes.sh"
install -m 644 "$root/src/tools/pkg/repo/_src/pipes.sh/upstream/LICENSE" "$stage/usr/share/licenses/pipes.sh/LICENSE"
