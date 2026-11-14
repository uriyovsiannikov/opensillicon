#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."

make iso src/build/user_start.o src/build/user_os.o src/build/user_path.o src/build/populate_root
out=src/build/usbx-check
mkdir -p "$out"
build_fixture() {
    name=$1
    shift
    ${CC:-cc} -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
        -fno-pic -fno-pie -mno-red-zone -Wall -Wextra -Werror -Isrc/include \
        "$@" -c src/tests/usbx_boot_log.c -o "$out/$name.o"
    ${LD:-ld} -nostdlib -static -no-pie -T src/user/c_linker.ld \
        src/build/user_start.o src/build/user_os.o src/build/user_path.o \
        "$out/$name.o" -o "$out/$name.elf"
    python3 src/tools/packers/mksilliconfs.py "$out/$name.img" --blocks 4096 --inodes 128
    src/build/populate_root "$out/$name.img" "$out/$name.elf" "$out/$name.elf"
}
build_fixture root
build_fixture keyboard -DUSBX_TEST_KEYBOARD
python3 src/tests/run_usbx_qemu.py
