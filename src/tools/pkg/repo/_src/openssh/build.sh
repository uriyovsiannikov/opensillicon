#!/bin/sh
# Experimental build only; see README.md before use.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../.." && pwd)
src="$root/src/tools/pkg/repo/_src/openssh/upstream"
out="$root/src/build/openssh-port"
cc=${CC:-gcc}
make -C "$root" src/build/musl/sysroot/lib/libc.a src/build/libz/libz.a
mkdir -p "$out"
# Autoconf normally executes its first test binary even with --host. Such a
# target binary can hang on Linux, so force its cross-compilation branch.
sed '0,/if test "$cross_compiling" != yes; then/s//cross_compiling=yes\nif test "$cross_compiling" != yes; then/' "$src/configure" > "$out/configure"
chmod +x "$out/configure"
cd "$out"
"$cc" -O2 -mno-red-zone -fno-stack-protector -fno-pie -nostdinc \
 -isystem "$root/src/build/musl/sysroot/include" -isystem "$($cc -print-file-name=include)" \
 -c "$root/src/tools/pkg/repo/_src/openssh/ppoll.c" -o "$out/opensillicon-ppoll.o"
CC="$cc" CFLAGS="-O2 -mno-red-zone -fno-stack-protector -fno-pie -U__linux__ -DOPEN_SILLICON -DNO_SA_RESTART -nostdinc -isystem $root/src/build/musl/sysroot/include -isystem $root/src/tools/pkg/repo/_src/twm/deps/libz -isystem $($cc -print-file-name=include)" \
LDFLAGS="-nostdlib -static -no-pie -Wl,-T,$root/src/user/c_linker.ld $root/src/build/musl/sysroot/lib/crt1.o $out/opensillicon-ppoll.o -L$root/src/build/libz -L$root/src/build/musl/sysroot/lib" \
LIBS='-lc -lgcc' ./configure --srcdir="$src" --build=x86_64-pc-linux-gnu --host=x86_64-unknown-none --prefix=/usr --sysconfdir=/etc/ssh --libexecdir=/usr/libexec --without-openssl --without-pam --without-hardening --without-zlib-version-check --disable-pkcs11 --disable-security-key > configure.log 2>&1 || { tail -40 configure.log; exit 1; }
make clean > clean.log 2>&1
"$cc" -O2 -mno-red-zone -fno-stack-protector -fno-pie -nostdinc \
 -isystem "$root/src/build/musl/sysroot/include" -isystem "$($cc -print-file-name=include)" \
 -c "$root/src/tools/pkg/repo/_src/openssh/ppoll.c" -o "$out/opensillicon-ppoll.o"
make -j"${JOBS:-4}" ssh ssh-keygen scp sftp ssh-agent ssh-add sshd sshd-session sshd-auth sftp-server > build.log 2>&1 || { tail -60 build.log; exit 1; }
