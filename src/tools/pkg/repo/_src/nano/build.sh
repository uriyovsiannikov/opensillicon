#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
cc=${CC:-gcc}
port="$root/src/tools/pkg/repo/_src/nano/upstream"
out="$root/src/build/nano"
mkdir -p "$out/app"
cflags='-D_GNU_SOURCE -O2 -std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -m64 -mno-red-zone'
cppflags="-nostdinc -I$root/src/build/musl/sysroot/include -isystem $($cc -print-file-name=include)"
cp "$port/config.h" "$out/app/config.h"
printf '#define REVISION "9.3 (OpenSillicon)"\n' > "$out/app/revision.h"
for source in "$port"/src/*.c; do
	name=$(basename "$source" .c)
	extra=
	if [ "$name" = nano ]; then extra=-Dmain=nano_main; fi
	"$cc" $cflags $cppflags -U__linux__ -DOPEN_SILLICON -DHAVE_CONFIG_H $extra \
		-I"$out/app" -I"$out/ncurses/include" -c "$source" -o "$out/app/$name.o"
done
"$cc" $cflags $cppflags -c "$port/main.c" -o "$out/app/port_main.o"
"$cc" -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
	-Wl,-T,"$root/src/user/c_linker.ld" "$root/src/build/musl/sysroot/lib/crt1.o" "$out"/app/*.o \
	-Wl,--start-group "$out/ncurses/lib/libncurses.a" "$root/src/build/musl/sysroot/lib/libc.a" \
	-Wl,--end-group -lgcc -o "$out/nano"
