#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$root/src/tools/builders/build_helpers.sh"
s=$root/src/user/thirdparty/netsurf
b=$root/src/build/netsurf
p=$b/prefix
x=$root/src/build/x11/prefix
cc=${CC:-gcc}
mkdir -p "$b" "$p"
exec 9>"$b/.build.lock"
flock 9
stamp="$b/.inputs"
temporary="$stamp.tmp.$$"
trap 'rm -f "$temporary"' EXIT HUP INT TERM
{
    find "$s" "$root/src/user/thirdparty/mbedtls" \
        "$root/src/user/thirdparty/xorg/deps/freetype" \
        "$root/src/user/libos/include" -type f ! -name '*.md' ! -name '*.pyc' -print0 | sort -z | xargs -0 sha256sum
    sha256sum "$0" "$root/src/tools/builders/build_mbedtls.sh" \
        "$root/src/build/libos/liblibos.a" "$root/src/build/libos/crt0.o" \
        "$root/src/build/libz/libz.a" "$x/lib/libxcb.a" "$x/lib/libXau.a" \
        "$x/lib/libpng.a" "$x/lib/libjpeg.a"
    "$cc" --version
} | sha256sum > "$temporary"
if [ -x "$b/netsurf" ] && [ -s "$b/resources/Messages" ] && \
    [ -f "$stamp" ] && cmp -s "$temporary" "$stamp"; then
    echo "NetSurf inputs unchanged; reusing XCB browser"
    exit 0
fi
if [ -f "$stamp" ] && [ -d "$b/source" ]; then
    # Make does not track compiler command changes. Keep the previous build
    # and compile a fresh source copy when any fingerprint input changes.
    python3 - "$b/source" <<'RESTART'
from pathlib import Path
import sys
import time
source = Path(sys.argv[1])
source.rename(source.with_name("source.previous." + str(time.time_ns())))
RESTART
fi
f="-Os -flto -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -DOPEN_SILLICON -nostdinc -isystem $($cc -print-file-name=include) -I$root/src/user/libos/include -I$root/src/user/thirdparty/xorg/deps/libxcb/include -I$p/include -I$x/include -I$root/src/user/thirdparty/xorg/deps/libz -idirafter /usr/include"
export PKG_CONFIG_LIBDIR="$p/lib/pkgconfig:$x/lib/pkgconfig:$x/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
# Describe the actual native XCB build, which has no XDMCP or host pthread libs.
mkdir -p "$p/lib/pkgconfig"
for pc in xcb xcb-shm xau; do
    sed '/^Requires/d' "$x/lib/pkgconfig/$pc.pc" > "$p/lib/pkgconfig/$pc.pc"
done
printf 'Requires.private: xau\n' >> "$p/lib/pkgconfig/xcb.pc"
printf 'Requires: xcb\n' >> "$p/lib/pkgconfig/xcb-shm.pc"
sed 's/-I${includedir}/-idirafter ${includedir}/' /usr/share/pkgconfig/xproto.pc > "$p/lib/pkgconfig/xproto.pc"
cat > "$p/lib/pkgconfig/zlib.pc" <<ZLIB_PC
Name: zlib
Description: Native zlib
Version: $(sed -n 's/^#define ZLIB_VERSION "\(.*\)"/\1/p' "$root/src/user/thirdparty/xorg/deps/libz/zlib.h")
Libs: -L$root/src/build/libz -lz
Cflags: -I$root/src/user/thirdparty/xorg/deps/libz
ZLIB_PC
triplet=$(sh "$root/src/user/thirdparty/xorg/deps/libX11/config.guess")
# Build release sources in the build directory, keeping the vendored tree clean.
mkdir -p "$b/source"
cp -a "$s/buildsystem" "$b/source/"
for library in libwapcaplet libparserutils libhubbub libdom libcss libnsbmp libnsgif libnsutils libnspsl libutf8proc libnsfb libsvgtiny; do
    if [ ! -f "$b/source/$library/Makefile" ]; then cp -a "$s/$library" "$b/source/"; fi
done
export PATH="$b/tools/bin:$PATH"
if [ ! -x "$b/tools/bin/gperf" ]; then
    mkdir -p "$b/gperf"
    cd "$b/gperf"
    build_component "HOST gperf configure" "$s/deps/gperf/configure" --prefix="$b/tools"
    build_component "HOST gperf support" make -j8 -C lib
    build_component "HOST gperf" make -j8 -C src
    build_component "HOST gperf install" make -C src install
fi
for item in xcb-util-0.4.1 xcb-util-image-0.4.1 xcb-util-wm-0.4.2 xcb-util-keysyms-0.4.1; do
    mkdir -p "$b/$item"
    cd "$b/$item"
    if [ ! -f Makefile ]; then
        CFLAGS="$f" LDFLAGS="-L$p/lib -L$x/lib" \
        build_component "LIB $item configure" "$s/deps/$item/configure" \
            --build="$triplet" --host=x86_64-elf --prefix="$p" --disable-shared --enable-static --disable-devel-docs
    fi
    case "$item" in
        xcb-util-0.4.1) dirs=src ;;
        xcb-util-image-*) dirs=image ;;
        xcb-util-wm-*) dirs="icccm ewmh" ;;
        xcb-util-keysyms-*) dirs=keysyms ;;
    esac
    build_component "LIB $item" make -j8 SUBDIRS="$dirs"
    build_component "LIB $item install" make SUBDIRS="$dirs" install
 done
mkdir -p "$b/expat"
# Expat's CMake probe cannot read the byte-order marker from LTO objects.
# The native target is x86-64 little endian.
build_component "LIB Expat configure" cmake -S "$s/deps/expat" -B "$b/expat" \
    -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_C_BYTE_ORDER=LITTLE_ENDIAN \
    -DCMAKE_C_COMPILER="$cc" -DCMAKE_C_FLAGS="$f" -DCMAKE_INSTALL_PREFIX="$p" \
    -DEXPAT_SHARED_LIBS=OFF -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_EXAMPLES=OFF \
    -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_DOCS=OFF -DEXPAT_WITH_GETRANDOM=OFF \
    -DEXPAT_WITH_SYS_GETRANDOM=OFF -DEXPAT_DEV_URANDOM=ON
build_component "LIB Expat" cmake --build "$b/expat" -j8
build_component "LIB Expat install" cmake --install "$b/expat"
for library in libwapcaplet libparserutils libhubbub libdom libcss libnsbmp libnsgif libnsutils libnspsl libutf8proc libsvgtiny libnsfb; do
    CFLAGS="$f" build_component "LIB $library" make -j8 -C "$b/source/$library" \
        PREFIX="$p" NSSHARED="$b/source/buildsystem" CC="$cc" AR=ar \
        HOST=x86_64-elf BUILD="$triplet" VARIANT=release \
        WITH_LIBXML_BINDING=no WITH_EXPAT_BINDING=yes WITH_HUBBUB_BINDING=yes \
        NSFB_SDL_AVAILABLE=no NSFB_WLD_AVAILABLE=no NSFB_VNC_AVAILABLE=no install
 done
# TLS uses the same native entropy, clock and Mozilla roots as the OS.
build_component "LIB TLS" sh "$root/src/tools/builders/build_mbedtls.sh"
tls=$root/src/build/mbedtls
tls_source=$root/src/user/thirdparty/mbedtls
tls_port=$root/src/user/thirdparty/mbedtls/opensillicon
# Link feature probes against libos, so host glibc cannot advertise functions
# that the native runtime does not implement.
cat > "$b/native-cc" <<WRAPPER
#!/bin/sh
for argument do
    case "\$argument" in
        -c|-E|-S|-M|-MM|--version|-dumpmachine|-dumpversion|-dumpfullversion|-print-file-name=*) exec "$cc" "\$@" ;;
    esac
done
exec "$cc" "\$@" -nostdlib -static -no-pie -Wl,--build-id=none \\
    -Wl,-T,$root/src/user/c_linker.ld "$root/src/build/libos/crt0.o" \\
    "$root/src/build/libos/liblibos.a" -lgcc
WRAPPER
chmod +x "$b/native-cc"
build_component "LIB curl configure" cmake -S "$s/deps/curl" -B "$b/curl" \
    -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_C_COMPILER="$b/native-cc" \
    -DCMAKE_C_FLAGS="$f -D_POSIX_C_SOURCE=200809L -I$tls_port -include $s/opensillicon/tls_config.h" \
    -DCMAKE_EXE_LINKER_FLAGS="" \
    -DCMAKE_C_STANDARD_LIBRARIES="" \
    -DCMAKE_INSTALL_PREFIX="$p" -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CURL_EXE=OFF -DPICKY_COMPILER=OFF \
    -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF \
    -DHAVE_GETADDRINFO_THREADSAFE=1 -DENABLE_THREADED_RESOLVER=OFF -DENABLE_IPV6=OFF -DCURL_DISABLE_SOCKETPAIR=ON \
    -DCURL_USE_OPENSSL=OFF -DCURL_USE_MBEDTLS=ON -DCURL_USE_LIBPSL=OFF \
    -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBSSH=OFF -DCURL_USE_GSSAPI=OFF \
    -DUSE_LIBIDN2=OFF -DUSE_NGHTTP2=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
    -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DCURL_DISABLE_FTP=ON \
    -DCURL_DISABLE_TFTP=ON -DCURL_DISABLE_SMTP=ON -DCURL_DISABLE_IMAP=ON \
    -DCURL_DISABLE_POP3=ON -DCURL_DISABLE_SMB=ON -DCURL_DISABLE_TELNET=ON \
    -DCURL_DISABLE_RTSP=ON -DCURL_DISABLE_DICT=ON -DCURL_DISABLE_GOPHER=ON \
    -DCURL_DISABLE_MQTT=ON -DCURL_DISABLE_DOH=ON \
    -DCURL_CA_BUNDLE=/etc/ssl/cert.pem -DCURL_CA_PATH=none \
    -DCURL_ZLIB=ON -DZLIB_LIBRARY="$root/src/build/libz/libz.a" \
    -DZLIB_INCLUDE_DIR="$root/src/user/thirdparty/xorg/deps/libz" \
    -DMBEDTLS_INCLUDE_DIR="$tls_source/include" -DMBEDTLS_LIBRARY="$tls/libmbedtls.a" \
    -DMBEDX509_LIBRARY="$tls/libmbedx509.a" -DMBEDCRYPTO_LIBRARY="$tls/libmbedcrypto.a"
build_component "LIB curl" cmake --build "$b/curl" -j8
build_component "LIB curl install" cmake --install "$b/curl"
# CMake's compiler-probe runtime must not become a -l/path entry in pkg-config.
sed -i '/^Libs:/c\Libs: -L${libdir} -lcurl' "$p/lib/pkgconfig/libcurl.pc"
sed -i '/^Libs.private:/c\Libs.private: -lmbedtls -lmbedx509 -lmbedcrypto -lz' "$p/lib/pkgconfig/libcurl.pc"
# A private FreeType build keeps only font drivers used by the shipped TTFs.
# This leaves the full FreeType build used by feh unchanged.
build_component "LIB browser FreeType configure" cmake \
    -S "$root/src/user/thirdparty/xorg/deps/freetype" -B "$b/freetype" \
    -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_C_COMPILER="$cc" -DCMAKE_C_FLAGS="$f -I$s/opensillicon -include $s/opensillicon/freetype_config.h" \
    -DCMAKE_INSTALL_PREFIX="$p" -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON \
    -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
build_component "LIB browser FreeType" cmake --build "$b/freetype" -j8
build_component "LIB browser FreeType install" cmake --install "$b/freetype"
build_component "LIB native runtime" make -C "$root" -j8 src/build/libos/liblibos.a
# Supply native implementations for libraries requested by upstream makefiles.
ar rcs "$p/lib/libm.a" "$root/src/build/libos/math.o"
ar rcs "$p/lib/libiconv.a" "$root/src/build/libos/iconv.o"
"$cc" $f -c "$s/opensillicon/runtime.c" -o "$b/runtime.o"
if [ ! -f "$b/source/netsurf/Makefile" ]; then cp -a "$s/netsurf" "$b/source/"; fi
cp "$s/opensillicon/Makefile.config" "$b/source/netsurf/Makefile.config"
# Hold native libraries in a group: their dependency graph contains cycles.
link="-flto -Os -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--gc-sections -Wl,-T,$s/opensillicon/linker.ld -Wl,--wrap=main $root/src/build/libos/crt0.o $b/runtime.o -L$p/lib -L$x/lib -L$root/src/build/libz -L$tls -Wl,--whole-archive $p/lib/libnsfb.a -Wl,--no-whole-archive -Wl,--start-group $root/src/build/libos/liblibos.a -lcss -ldom -lhubbub -lparserutils -lwapcaplet -lnsutils -lnspsl -lutf8proc -lnsbmp -lnsgif -lsvgtiny -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lexpat -lxcb-shm -lxcb-image -lxcb-icccm -lxcb-keysyms -lxcb-util -lxcb -lXau -lfreetype -lpng -ljpeg -lz -liconv -lgcc -Wl,--end-group"
CFLAGS="$f" \
    build_component "USER NetSurf XCB" make -j8 -C "$b/source/netsurf" \
    TARGET=framebuffer HOST=OpenSillicon CC="$cc" CXX="${CXX:-g++}" CCACHE= \
    BUILD_CC="$cc" CWARNFLAGS= CXXWARNFLAGS= COMMON_WARNFLAGS= LDFLAGS="$link" \
    NETSURF_BUILTIN_LOG_FILTER=level:WARNING \
    NETSURF_BUILTIN_VERBOSE_FILTER=level:INFO
objcopy --strip-all "$b/source/netsurf/nsfb" "$b/netsurf"
mkdir -p "$b/resources"
for item in adblock.css credits.html default.css internal.css licence.html netsurf.png quirks.css welcome.html; do
    cp "$b/source/netsurf/frontends/framebuffer/res/$item" "$b/resources/$item"
done
cp "$b/source/netsurf/frontends/framebuffer/res/en/Messages" "$b/resources/Messages"

python3 - "$b/netsurf" <<'CHECK_SIZE'
from pathlib import Path
import sys
size = Path(sys.argv[1]).stat().st_size
if size > 1036 * 4096:
    raise SystemExit(f"NetSurf binary exceeds the SilliconFS file limit: {size} bytes")
CHECK_SIZE
mv "$temporary" "$stamp"
