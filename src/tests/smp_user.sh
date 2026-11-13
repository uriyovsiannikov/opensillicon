#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
# Disposable images: the normal root image is never modified.
make -j8 iso src/build/populate_root src/build/user_start.o src/build/user_os.o src/build/user_path.o
mkdir -p src/build/smp-user
if [ "$#" -eq 0 ]; then
    set -- private_memory shared_memory signals pipes timers queues stealing
fi
for fixture in "$@"; do
    case "$fixture" in
        private_memory|shared_memory|signals|pipes|timers|queues|stealing) ;;
        *) printf 'Unknown SMP fixture: %s\n' "$fixture" >&2; exit 2 ;;
    esac
    ${CC:-cc} -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
        -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=small -Wall -Wextra -Werror \
        -Isrc/include -Isrc/user -c "src/tests/smp_${fixture}.c" \
        -o "src/build/smp-user/${fixture}.o"
    ${LD:-ld} -nostdlib -static -no-pie -T src/user/c_linker.ld \
        src/build/user_start.o src/build/user_os.o src/build/user_path.o \
        "src/build/smp-user/${fixture}.o" -o "src/build/smp-user/${fixture}.elf"
    python3 src/tools/packers/mksilliconfs.py "src/build/smp-user/${fixture}.img" \
        --blocks 4096 --inodes 128
    src/build/populate_root "src/build/smp-user/${fixture}.img" \
        "src/build/smp-user/${fixture}.elf" "src/build/smp-user/${fixture}.elf"
done
python3 src/tests/run_smp_user_qemu.py "$@"
