# Packages

The system package manager supports four commands.

```sh
doas pkg repo https://192.168.1.112:8080
doas pkg update
doas pkg add hello
hello
doas pkg remove hello
```

Application binaries are packaged at `usr/bin/<name>` and installed at
`/usr/bin/<name>`. The shell, command completion, `which` and libos program
lookup include `/usr/bin`. Live mode stores installed files in RAM until
reboot. A disk installation stores them persistently.

The repository URL is saved in `/etc/pkg.conf`. The downloaded index is
`/var/cache/pkg/index`, and installation records are stored in
`/var/db/pkg/<name>.db`. These operations require root. The default
[/etc/doas.conf](../doas/README.md) permits UID 1000 to run `/bin/pkg` as root.

`pkg add name` downloads a package named in the saved repository index.
`pkg add ./file.spkg` installs a local archive. Run `pkg update` first for
repository installation.

`pkg remove name` verifies present files against the installation record
before removing them. Modified files cause removal to fail. Missing files are
accepted so an interrupted removal can be retried. Empty directories are
retained. An incomplete installation's partial files can be removed if they
still have the installer's private mode and ownership. Existing records
retain their original paths for removal.

## Host repository

```sh
make src/build/pkg/pkg-server src/tools/pkg/repo/_pkges/hello-1.0.0.spkg
src/build/pkg/pkg-server --repo src/tools/pkg/repo --bind 0.0.0.0 --port 8080 \
  --cert /absolute/path/fullchain.pem --key /absolute/path/privkey.pem
```

The default configuration points at QEMU's `https://10.15.3.2:8080`.
On physical hardware, configure the reachable LAN address or DNS name of the
host. The server certificate must cover the chosen name or IP in its SANs.
Without an explicit port, HTTPS uses 443 and HTTP uses 80.

TLS uses the shared Mbed TLS library. The client verifies the certificate
chain, server name/IP and expiry against `/etc/ssl/cert.pem`, using the native
realtime clock and `/dev/urandom`. Failures abort the download. HTTPS is never
retried over plaintext, and redirects are not followed.

For a private CA, install its PEM certificate at `/etc/ssl/pkg-ca.pem` as root,
or include it when building the Live/installed root image:

```sh
make PKG_CA_CERT=/absolute/path/repository-ca.pem live-iso
```

This supplements public roots; no insecure verification bypass is provided.
The server's `--cert` file contains its leaf certificate and intermediate
chain, and `--key` is its unencrypted PEM private key, kept private on the host.

Explicit `http://` URLs remain available for local development and old
configurations. The host server uses HTTP only when neither `--cert` nor
`--key` is supplied; supplying just one is an error. New system images default
to HTTPS. The four package commands are unchanged.

The server exposes `GET /index` and `GET /packages/<name>-<version>.spkg`.
Archives belong in `src/tools/pkg/repo/_pkges/`. Sources and build recipes
belong in `src/tools/pkg/repo/_src/<name>/`.

## Package creation

Compile native OpenSillicon programs against libos, then create a staging
directory with paths such as `usr/bin/example` and `usr/share/example/data`.

```sh
make src/build/pkg/pkg-build
src/build/pkg/pkg-build example 1.0 /absolute/path/to/stage \
  src/tools/pkg/repo/_pkges/example-1.0.spkg 'Example application'
```

The builder accepts regular files under `usr/` and `opt/`. Executable files
receive mode 0755 and other files receive mode 0644. Archive paths under
`bin/` are rejected. Paths are used directly, without relocation during
installation. Links, special files, absolute paths and parent traversal are
rejected.

## Installation integrity

Before writing files, the client validates the archive, file hashes,
destination paths, existing files and reservations held by other packages.
Repository downloads must match the index's size, hash, name and version.

An exclusive database lock serializes installation and removal. The client
writes and flushes the manifest before payloads, then flushes payloads before
marking the record installed. An interrupted installation retains its record
and written files. The record is removed after successful file removal.
The whole operation is not power-loss atomic.

HTTPS authenticates the repository and encrypts downloads. SHA-256 checks
also detect damaged or inconsistent payloads. Repository signing is not
implemented. Limits are 16 MiB and 128 files per archive, 256 packages and
256 KiB per index. Connections and TLS I/O share a 30-second deadline;
only Content-Length responses without transfer/content encoding are accepted.

## HTTPS verification

```sh
python3 src/tests/pkg_paths.py
python3 src/tests/pkg_https.py
python3 src/tests/pkg_https.py --qemu
```

The host test covers update/add/remove, DNS/IP certificate checks, expiry,
untrusted issuers, truncation and downgrade rejection. `--qemu` builds its own
isolated Live image with a fixture CA and verifies native update/add/run/remove
and certificate rejection. Release images and system trust stores are not
modified. The host test client honors `SSL_CERT_FILE`; the native privileged
client ignores environment overrides and uses the fixed system trust paths.

## Checks

The host client accepts `-r /absolute/test/root` for isolated integration tests.

```sh
make src/build/pkg/pkg-host src/build/pkg/pkg-build
python3 src/tests/pkg_paths.py
```
