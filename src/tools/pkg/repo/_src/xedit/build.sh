#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
src="$root/src/tools/pkg/repo/_src/xedit/upstream"
out="$root/src/build/x11/clients/xedit-port"
stage="$root/src/build/pkg/xedit-stage"
cc=${CC:-gcc}
make -C "$root" src/build/x11/.apps-ready
mkdir -p "$out" "$stage/usr/bin" "$stage/usr/share/licenses/xedit" "$stage/usr/share/X11/app-defaults" "$stage/usr/share/xedit"
cflags="-std=gnu11 -O2 -DNDEBUG -DOPEN_SILLICON -D_DEFAULT_SOURCE -D_BSD_SOURCE -DXEDIT -DLISP -DO_NOCTTY=0 -DWUNTRACED=0 -Dfinite=isfinite -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -m64 -mno-red-zone -nostdinc -I$root/src/include -I$root/src/build/musl/sysroot/include -I$root/src/user/thirdparty/musl/opensillicon/overlay -I$root/src/build/x11/prefix/include -I$root/src/build/x11/libX11/include -I$src -I$src/lisp -I$src/lisp/mp -I$src/lisp/re"
compile() {
	$cc $cflags -isystem "$($cc -print-file-name=include)" -idirafter /usr/include "$@"
}
for file in commands hash hook ispell lisp options tags util xedit; do
	if [ "$file" = xedit ]; then
		compile -DPACKAGE_STRING='"xedit 1.2.4"' -c "$src/$file.c" -o "$out/$file.o"
	else
		compile -c "$src/$file.c" -o "$out/$file.o"
	fi
done
for file in bytecode core debugger format hash helper io lisp math package pathname read regex require stream string struct time write xedit; do
	compile -c "$src/lisp/$file.c" -o "$out/lisp-$file.o"
done
for file in mp mpi mpr; do
	compile -c "$src/lisp/mp/$file.c" -o "$out/mp-$file.o"
done
for file in re rec reo; do
	compile -c "$src/lisp/re/$file.c" -o "$out/re-$file.o"
done
set --
for object in commands hash hook ispell lisp options tags util xedit; do set -- "$@" "$out/$object.o"; done
for object in bytecode core debugger format hash helper io lisp math package pathname read regex require stream string struct time write xedit; do set -- "$@" "$out/lisp-$object.o"; done
for object in mp mpi mpr; do set -- "$@" "$out/mp-$object.o"; done
for object in re rec reo; do set -- "$@" "$out/re-$object.o"; done
$cc -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,"$root/src/user/c_linker.ld" \
	"$root/src/build/musl/sysroot/lib/crt1.o" "$@" -L"$root/src/build/x11/prefix/lib" \
	-Wl,--start-group -lXaw7 -lXpm -lXmu -lXt -lSM -lICE -lXext -lX11 -lxcb -lXau \
	"$root/src/build/x11/prefix/lib/libi18n.a" "$root/src/build/musl/sysroot/lib/libc.a" -Wl,--end-group -lgcc -o "$out/xedit"
install -m 755 "$out/xedit" "$stage/usr/bin/xedit"
install -m 644 "$src/COPYING" "$stage/usr/share/licenses/xedit/COPYING"
install -m 644 "$src/app-defaults/Xedit" "$src/app-defaults/Xedit-color" "$stage/usr/share/X11/app-defaults/"
cp -R "$src/lisp/modules" "$stage/usr/share/xedit/lisp"
