#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
. "$root/src/tools/builders/build_helpers.sh"
cc=${CC:-gcc}
port="$root/src/tools/pkg/repo/_src/nano"
out="$root/src/build/nano"
src="$out/deps/ncurses-6.5"
mkdir -p "$out/deps" "$out/ncurses"
exec 9>"$out/.build.lock"
flock 9
archive="$port/upstream/deps/ncurses-6.5.tar.gz"
printf '%s  %s\n' 136d91bc269a9a5785e5f9e980bc76ab57428f604ce3e5a5a90cebc767971cc6 "$archive" | sha256sum -c -
if [ ! -f "$src/configure" ]; then
	tar -xzf "$archive" -C "$out/deps"
fi
# ncurses uses this clock for input deadlines. OpenSillicon provides CLOCK_MONOTONIC.
clock_header="$src/ncurses/curses.priv.h"
if grep -q 'GetClockTime(t) clock_gettime(CLOCK_REALTIME, t)' "$clock_header"; then
	sed -i 's/GetClockTime(t) clock_gettime(CLOCK_REALTIME, t)/GetClockTime(t) clock_gettime(CLOCK_MONOTONIC, t)/' "$clock_header"
fi
build_triplet=$(sh "$src/config.guess")
cflags='-O2 -std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -m64 -mno-red-zone'
cppflags="-nostdinc -I$root/src/build/musl/sysroot/include -isystem $($cc -print-file-name=include)"
# Configure flags and libc ABI changes require a clean dependency rebuild.
build_key=$( { printf '%s\n' "$cc" "$cflags" "$cppflags"; sha256sum "$0" "$root/src/build/musl/sysroot/lib/libc.a"; } | sha256sum)
if [ "$(cat "$out/ncurses/.musl-build-key" 2>/dev/null || true)" != "$build_key" ]; then
 rm -rf "$out/ncurses"
 mkdir -p "$out/ncurses"
fi
cd "$out/ncurses"
fallbacks=xterm,xterm-256color,screen,screen-256color,vt100,vt220,linux,dumb
if [ ! -f Makefile ] || [ "$(cat .opensillicon-fallbacks 2>/dev/null || true)" != "$fallbacks" ]; then
	CPPFLAGS="$cppflags" CFLAGS="$cflags" \
	LDFLAGS="-nostdlib -static -no-pie -Wl,-T,$root/src/user/c_linker.ld $root/src/build/musl/sysroot/lib/crt1.o" \
	LIBS="$root/src/build/musl/sysroot/lib/libc.a -lgcc" \
	build_component "LIB ncurses configure" "$src/configure" --build="$build_triplet" --host=x86_64-elf --prefix=/usr \
		--without-progs --without-tests --without-manpages --without-cxx \
		--without-cxx-binding --without-ada --without-debug --without-shared \
		--disable-widec --disable-database --with-fallbacks="$fallbacks"
	printf '%s\n' "$fallbacks" > .opensillicon-fallbacks
fi
# configure changes FALLBACK_LIST without making fallback.c depend on it.
fallback_words=$(printf '%s' "$fallbacks" | tr ',' ' ')
if ! grep -Fq "/* fallback entries for: $fallback_words */" ncurses/fallback.c 2>/dev/null; then
    rm -f ncurses/fallback.c
fi
# Header generators run on the build host; the library itself uses libc.
build_component "LIB ncurses headers" make -C include
build_component "LIB ncurses" make -C ncurses -j"${NANO_JOBS:-4}" \
	BUILD_CPPFLAGS="-DHAVE_CONFIG_H -DUSE_BUILD_CC -I. -I../include -I$src/ncurses -I$src/include" \
	BUILD_LDFLAGS= BUILD_LIBS=

printf '%s\n' "$build_key" > .musl-build-key
