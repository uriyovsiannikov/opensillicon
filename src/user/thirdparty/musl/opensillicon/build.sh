#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../.." && pwd)
build="$root/src/build/musl"
source="$root/src/user/thirdparty/musl"
port="$root/src/user/thirdparty/musl/opensillicon"
mkdir -p "$build/source" "$build/obj"
exec 9>"$build/.build.lock"
flock 9
# Preserve timestamps so repeated builds remain incremental.
tar -C "$source" --exclude=./opensillicon -cf - . | tar -C "$build/source" -xf -
cp -p "$port/include/sys/sysctl.h" "$build/source/include/sys/sysctl.h"
cp -p "$port/overlay/resolvconf.c" "$build/source/src/network/resolvconf.c"
cp -p "$port/overlay/syscall_arch.h" "$build/source/arch/x86_64/syscall_arch.h"
cp -p "$port/overlay/opensillicon.c" "$port/overlay/thread.c" "$port/overlay/network.c" "$port/overlay/sysctl.c" "$build/source/src/misc/"
mkdir -p "$build/source/src/misc/x86_64"
cp -p "$port/overlay/raw.s" "$port/overlay/cp.s" "$build/source/src/misc/x86_64/"
# These Linux assembly entry points bypass syscall_arch.h.
for name in syscall_cp __set_thread_area clone __unmapself; do
 rm -f "$build/source/src/thread/x86_64/$name.s"
done
# Return ENOSYS through musl's errno conversion, rather than a Linux trap.
rm -f "$build/source/src/process/x86_64/vfork.s"
cat > "$build/source/src/process/x86_64/vfork.c" <<'C'
#include <errno.h>
#include <unistd.h>
pid_t vfork(void) { errno=ENOSYS; return -1; }
C
# The kernel validates the exact native restorer instruction sequence.
cp -p "$port/overlay/restore.s" "$build/source/src/signal/x86_64/restore.s"
cd "$build/obj"
if [ ! -f config.mak ] || [ "$port/build.sh" -nt config.mak ]; then
 AR="${AR:-ar}" RANLIB="${RANLIB:-ranlib}" CC="${CC:-gcc}" CFLAGS="-O2 -mno-red-zone -fno-stack-protector -I$root/src/include" \
  ../source/configure --target=x86_64 --disable-shared --prefix="$build/sysroot" > configure.log
fi
make -j"${JOBS:-4}" > build.log 2>&1 || { tail -60 build.log; exit 1; }
make install > install.log 2>&1
cp -R "$port/include/." "$build/sysroot/include/"

install -D -m 644 "$root/src/include/abi/sysctl.h" "$build/sysroot/include/abi/sysctl.h"
