# OpenSSH for OpenSillicon (experimental)

OpenSSH portable 10.6p2, from
https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-10.6p2.tar.gz
(SHA-256 `9ae44872090cf2f06b3cf9ff147e7f16f309671340763ed2f99248398ae29d5b`).
The release signature was verified with the upstream release key fingerprint
`7168 B983 815A 5EEF 59A4 ADFD 2A3F 414E 7360 60BA` from
https://www.openssh.com/portable.html. `auth-passwd.c` includes `<stdlib.h>`
for the target C compiler. In `sshd-session.c`, cancellation of the login
timer is conditional on that timer having been armed; this allows the
explicit `LoginGraceTime 0` test configuration to run on this OS.

`make -C src/tools/pkg/repo/_src/openssh` builds the client and experimental
server binaries, then creates the local `openssh-client-10.6.2.spkg`
package containing ssh, ssh-keygen, scp, sftp, ssh-agent, and ssh-add. The
package is not published automatically. It requires a kernel and musl with the
socketpair syscall introduced alongside this port.

`python3 src/tools/pkg/repo/_src/openssh/tests/smoke.py` boots the actual OS in
QEMU, generates an Ed25519 key, verifies a temporary host's key, authenticates
using a separate temporary Ed25519 key, runs a remote command, and transfers a
file with scp. It requires KVM, OVMF, xorriso, host OpenSSH server and client,
and the e1000 module. Test credentials are generated afresh; test images
in the build directory contain copies of those disposable credentials. The
test also checks ssh-agent, ssh-add and `sshd -t`. Interactive terminal usage
is not yet tested.

The build uses upstream's experimental `--without-openssl` configuration;
only its limited internal algorithms are available. PKCS#11 and security keys
are disabled. The port supplies a temporary `ppoll` adapter over `poll`. With a
signal mask, it wakes at least every 100 ms to limit the non-atomic mask-change
race; this is not equivalent to atomic `ppoll` and remains to be fixed in the
kernel. Consequently this port should still be considered experimental.

The server binaries are built but intentionally not included in the client
package: `sshd` privilege separation needs OS facilities including `chroot`
that have not been implemented and tested. An isolated QEMU probe using an
unprivileged server and `LoginGraceTime 0` checks inbound key authentication
and command execution; it does not test the normal root privilege separation
or timeout. Do not install or run these binaries as a production SSH server.
No host keys are shipped in the package.
