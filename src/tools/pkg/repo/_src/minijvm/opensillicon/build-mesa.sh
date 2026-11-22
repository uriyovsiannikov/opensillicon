#!/bin/sh
set -eu
archive=$1
build=$2
headers=$3
cc=$4
host_cc=$5

# Keep the generator native to the build host. Only the library uses libc.
mkdir -p "$build"
tar -xm -f "$archive" -C "$build"
cd "$build/Mesa-7.8.2"
cp configs/linux-osmesa-static configs/current
mkdir -p lib
$host_cc -std=gnu99 -O2 src/glsl/apps/compile.c src/glsl/cl/sl_cl_parse.c \
    src/glsl/pp/*.c -o src/glsl/apps/compile
make -C src/mesa/shader/slang/library

gcc_headers=$($cc -print-file-name=include)
flags="-std=gnu99 -O2 -fcommon -ffreestanding -fno-builtin -fno-strict-aliasing \
    -fno-pie -fno-stack-protector -mno-red-zone -nostdinc -I$headers -isystem $gcc_headers \
    -U__linux__ -U__gnu_linux__ -Dffs=__builtin_ffs"
make -C src/glsl/pp CC="$cc" CFLAGS="$flags" MKDEP=true libglslpp.a
make -C src/glsl/cl CC="$cc" CFLAGS="$flags" MKDEP=true libglslcl.a
make -C src/mesa CC="$cc" CFLAGS="$flags" MKDEP=true libmesa.a libglapi.a
make -C src/mesa/drivers/osmesa CC="$cc" CFLAGS="$flags" MKDEP=true
