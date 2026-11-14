# doas

`/usr/bin/doas` runs commands permitted by the root-owned
`/etc/doas.conf`. It is installed with owner root and mode 4755.
The kernel applies set-ID executable credentials after successful ELF loading.

The shipped configuration permits UID 1000 to run the package manager.

```text
permit nopass 1000 as root cmd /bin/pkg
```

```sh
doas pkg update
doas pkg add hello
doas pkg remove hello
```

The supported rule form is a subset of the
[OpenBSD doas.conf syntax](https://man.openbsd.org/doas.conf).

```text
permit nopass identity as root cmd /absolute/command [args ...]
deny identity as root cmd /absolute/command [args ...]
```

Numeric identities and names resolved by libos are accepted. The current
libos account lookup resolves root. The last matching rule decides whether
execution is permitted. An explicit `args` clause requires an exact argument
match. With `args` alone, the command must receive no arguments.
Blank lines and hash comments are accepted. Unsupported syntax rejects the
configuration. Password authentication, quoting, groups and other OpenBSD
rule options are not implemented.

Commands without a slash are resolved through `/bin`, `/usr/bin`, then
`/sbin`. Configuration and command paths must be root-owned and protected
from writes by other users. Extra descriptors are closed before execution,
the umask is reset to 022, and all UID/GID values are set to root.
The native exec ABI starts the new process with an empty environment.

`doas -n command` accepts the same passwordless rules.
