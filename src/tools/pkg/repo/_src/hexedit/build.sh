#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
src="$root/src/tools/pkg/repo/_src/hexedit/upstream"
out="$root/src/build/hexedit"
stage="$root/src/build/pkg/hexedit-stage"
cc=${CC:-gcc}
mkdir -p "$out" "$stage/usr/bin" "$stage/usr/share/licenses/hexedit" "$stage/usr/share/man/man1"
cflags="-std=c99 -O2 -DNDEBUG -D_POSIX_C_SOURCE=200809L -DOPEN_SILLICON -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -m64 -mno-red-zone -nostdinc -I$root/src/include -I$root/src/build/musl/sysroot/include -I$root/src/user/thirdparty/musl/opensillicon/overlay"
for file in hx editor charbuf util undo; do
	$cc $cflags -DHX_VERSION='"1.0.0"' -DHX_GIT_HASH='"opensillicon"' \
		-isystem "$($cc -print-file-name=include)" -I"$src" -c "$src/$file.c" -o "$out/$file.o"
done
$cc -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,"$root/src/user/c_linker.ld" \
	"$root/src/build/musl/sysroot/lib/crt1.o" "$out/hx.o" "$out/editor.o" "$out/charbuf.o" "$out/util.o" "$out/undo.o" \
	"$root/src/build/musl/sysroot/lib/libc.a" -lgcc -o "$out/hexedit"
install -m 755 "$out/hexedit" "$stage/usr/bin/hexedit"
install -m 644 "$src/LICENSE" "$stage/usr/share/licenses/hexedit/LICENSE"
install -m 644 "$src/hx.1" "$stage/usr/share/man/man1/hexedit.1"
