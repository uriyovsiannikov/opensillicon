#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
target=${1:-native}
case "$target" in
    native) cc=${CC:-gcc}; build=$root/src/build/mbedtls ;;
    host) cc=${HOST_CC:-cc}; build=$root/src/build/mbedtls-host ;;
    *) echo "usage: $0 [native|host]" >&2; exit 2 ;;
esac
source=$root/src/user/thirdparty/mbedtls
port=$root/src/user/thirdparty/mbedtls/opensillicon
mkdir -p "$build"
exec 9>"$build/.build.lock"
flock 9
stamp="$build/.inputs"
temporary="$stamp.tmp.$$"
trap 'rm -f "$temporary"' EXIT HUP INT TERM
{
    find "$source/include" "$source/library" "$source/3rdparty" \
        "$root/src/build/musl/sysroot/include" -type f -print0 |
        sort -z | xargs -0 sha256sum
    sha256sum "$0" "$port/opensillicon_mbedtls_config.h" "$port/tls_platform.c"
    "$cc" --version
} | sha256sum >"$temporary"
if [ -f "$build/libmbedtls.a" ] && [ -f "$stamp" ] &&
    cmp -s "$temporary" "$stamp"; then
    exit 0
fi

flags="-std=c11 -D_POSIX_C_SOURCE=200809L -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -nostdinc -isystem $("$cc" -print-file-name=include) -I$root/src/build/musl/sysroot/include -I$source/include -I$source/library -I$source/3rdparty/everest/include -I$port -DMBEDTLS_CONFIG_FILE=\"opensillicon_mbedtls_config.h\""
if [ "$target" = host ]; then
    flags="-std=c11 -O2 -D_POSIX_C_SOURCE=200809L -I$source/include -I$source/library -I$source/3rdparty/everest/include -I$port -DMBEDTLS_CONFIG_FILE=\"opensillicon_mbedtls_config.h\""
fi
for file in "$source"/library/*.c "$port/tls_platform.c"; do
    object="$build/$(basename "$file" .c).o"
    "$cc" $flags -c "$file" -o "$object"
done
python3 - "$source" "$build" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

source, build = map(Path, sys.argv[1:])
makefile = (source / "library/Makefile").read_text()
groups = {}
for name in ("TLS", "X509"):
    section = re.search(r"^OBJS_" + name +
                        r"=([\s\S]*?)# This line", makefile, re.M).group(1)
    groups[name] = [build / name for name in re.findall(r"\b\w+\.o\b", section)]
groups["CRYPTO"] = sorted(set(build.glob("*.o")) -
                          set(groups["TLS"]) - set(groups["X509"]))
for name, library in (("TLS", "mbedtls"), ("X509", "mbedx509"),
                      ("CRYPTO", "mbedcrypto")):
    temporary = build / (library + ".tmp.a")
    temporary.unlink(missing_ok=True)
    subprocess.run(["ar", "rcs", str(temporary), *map(str, groups[name])],
                   check=True)
    temporary.replace(build / ("lib" + library + ".a"))
PY
mv "$temporary" "$stamp"
