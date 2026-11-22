#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
stamp="$root/src/build/x11/.apps-input.sha256"
tmp="$stamp.tmp.$$"
link_stamp="$stamp.link"
link_tmp="$link_stamp.tmp.$$"
trap 'rm -f "$tmp" "$link_tmp"' EXIT HUP INT TERM
mkdir -p "$root/src/build/x11"
# Separate make processes share the same Autoconf work directories and stamps.
# Hold the lock through both fingerprinting and publication of the build.
exec 9>"$root/src/build/x11/.build.lock"
flock 9

{
	find \
		"$root/src/tools/pkg/repo/_src/twm/deps/libXau" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libxcb" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libX11" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libXext" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libICE" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libSM" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libXt" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libXmu" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libXpm" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libXaw" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libxtrans" \
		"$root/src/tools/pkg/repo/_src/twm/deps/xcb-proto" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libXfixes-6.0.2" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libXi-1.8.4" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libpng" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libz" \
		"$root/src/tools/pkg/repo/_src/twm/deps/freetype" \
		"$root/src/tools/pkg/repo/_src/twm/deps/libjpeg-turbo" \
		"$root/src/tools/pkg/repo/_src/twm/deps/imlib2" \
		"$root/src/tools/pkg/repo/_src/twm/twm" \
		"$root/src/tools/pkg/repo/_src/twm/xterm" \
		"$root/src/tools/pkg/repo/_src/twm/feh" \
		"$root/src/build/musl/sysroot/include" \
		"$root/src/build/nano/ncurses/include" \
		"$root/src/include/abi" \
		"$root/src/tools/builders/build_x11_if_needed.sh" \
		"$root/src/tools/builders/build_x11_apps.sh" \
		"$root/src/tools/builders/build_feh.sh" \
		"$root/src/user/c_linker.ld" \
		-type d \( -name .git -o -name __pycache__ \) -prune -o -type f ! -name '*.pyc' -print0 2>/dev/null | sort -z | xargs -0 sha256sum
} | sha256sum | cut -d ' ' -f 1 > "$tmp"

# Implementation changes in libc/zlib require relinking, not reconfiguring
# every X11 dependency. Public headers remain part of the source fingerprint.
sha256sum "$root/src/build/musl/sysroot/lib/libc.a" "$root/src/build/musl/sysroot/lib/crt1.o" \
    "$root/src/build/libz/libz.a" "$root/src/build/nano/ncurses/lib/libncurses.a" \
    "$root/src/user/c_linker.ld" > "$link_tmp"

mode=full
if [ -f "$stamp" ] && cmp -s "$tmp" "$stamp" &&
	[ -x "$root/src/build/tinyx/target/kdrive/opensillicon/X" ] &&
	[ -x "$root/src/build/x11/twm/src/twm" ] &&
	[ -x "$root/src/build/x11/xterm/xterm" ] &&
	[ -x "$root/src/build/x11/feh/src/feh" ]; then
	if [ -f "$link_stamp" ] && cmp -s "$link_tmp" "$link_stamp"; then
		echo "X11 inputs unchanged; reusing existing applications"
		exit 0
	fi
	mode=relink
fi

echo "X11: $mode build"
if sh "$root/src/tools/builders/build_x11_apps.sh" "$mode"; then
	mv "$tmp" "$stamp"
	mv "$link_tmp" "$link_stamp"
else
	rm -f "$tmp"
	exit 1
fi
