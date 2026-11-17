#!/bin/sh
set -eu
src=$1
build=$2
cc=$3
libos=$4
crt=$5
shift 5
x11=$1
mesa=$2
shift 2
mkdir -p "$build"

# Compile with SSE for Java floating-point bytecodes.
gcc_headers=$("$cc" -print-file-name=include)
set -- "$@" "$src/vendor/main.c" "$src/opensillicon/"*.c "$src/opensillicon/context.S"
for input in "$src/vendor/jvm/"*.c "$src/vendor/utils/"*.c; do
    case "$input" in
        */jdwp.c|*/tinycthread.c) ;;
        *) set -- "$@" "$input" ;;
    esac
done
"$cc" -std=gnu11 -O2 -ffreestanding -fno-builtin -fno-strict-aliasing \
    -fwrapv -fno-pic -fno-pie -fno-stack-protector -mno-red-zone \
    -nostdinc -Isrc/user/libos/include -isystem "$gcc_headers" -idirafter /usr/include \
    -I"$src/vendor/jvm" -I"$src/vendor/utils" -I"$x11/include" -I"$mesa/include" \
    -Isrc/user/thirdparty/mbedtls/include -Isrc/user/thirdparty/mbedtls/opensillicon \
    -DMBEDTLS_CONFIG_FILE='"opensillicon_mbedtls_config.h"' \
    -Isrc/tools/pkg/repo/_src/twm/deps/libz -DOPEN_SILLICON -DMINIZ_NO_TIME \
    -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack \
    -Wl,-T,src/user/c_linker.ld "$crt" -Wl,--start-group "$@" "$libos" \
    -Wl,--end-group -lgcc -o "$build/java"
