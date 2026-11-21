#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
out="$root/src/build/cowsay"
stage="$root/src/build/pkg/cowsay-stage"
mkdir -p "$out" "$stage/usr/bin" "$stage/usr/share/licenses/cowsay"
cc=${CC:-gcc}
"$cc" -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -m64 -mno-red-zone \
	-nostdinc -I"$root/src/include" -I"$root/src/user/libos/include" \
	-isystem "$($cc -print-file-name=include)" -c "$root/src/tools/pkg/repo/_src/cowsay/opensillicon.c" -o "$out/cowsay.o"
"$cc" -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,"$root/src/user/c_linker.ld" \
	"$root/src/build/libos/crt0.o" "$out/cowsay.o" "$root/src/build/libos/liblibos.a" -lgcc -o "$out/cowsay"
install -m 755 "$out/cowsay" "$stage/usr/bin/cowsay"
install -m 755 "$out/cowsay" "$stage/usr/bin/cowthink"
install -m 644 "$root/src/tools/pkg/repo/_src/cowsay/upstream/LICENSE.txt" "$stage/usr/share/licenses/cowsay/LICENSE.txt"
