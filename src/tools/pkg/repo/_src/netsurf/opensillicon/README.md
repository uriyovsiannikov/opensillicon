# NetSurf XCB for OpenSillicon

NetSurf 3.11 uses its framebuffer frontend in an X11 window through the native
XCB surface in libnsfb. It does not access the console framebuffer directly.
It is provided by the optional `netsurf` package and the twm menu. Build the
package with `make -C src/tools/pkg/repo/_src/netsurf`; install `xorg` first,
then launch it with `netsurf` or `netsurf https://example.org/`. `-f x`
explicitly selects XCB.
The default homepage is `https://frogfind.com/`.

The native binary is
`src/build/netsurf/netsurf`; the unstripped executable is
`src/build/netsurf/source/netsurf/nsfb`. The builder fingerprints sources,
compiler and native libraries, uses a lock, and builds in `src/build/netsurf`.
Sources, versions and archive checksums are in SOURCES.md.

## Included capabilities

* HTML/CSS through libhubbub, libdom and libcss; PNG, JPEG, GIF, BMP and SVG.
* Antialiased FreeType text using bundled Liberation Sans, Serif and Mono,
  including Cyrillic, bold and italic. A private TrueType-only FreeType module
  set reduces size; feh retains its existing full FreeType build.
* libcurl 8.23.0 with Mbed TLS 3.6.7, native entropy, synchronous IPv4 DNS,
  hostname/chain/date verification and the shared Mozilla certificate bundle.
* IDN processing with utf8proc, public suffix handling, normal XCB keyboard
  and mouse input, and WM_DELETE_WINDOW handling in twm.
* `/usr/etc/netsurf/Choices` holds defaults. Resources are installed in
  `/usr/share/netsurf`, fonts in `/usr/share/fonts/truetype/liberation`, and
  trust roots in `/etc/ssl/cert.pem`.

JavaScript, WebP, JPEG XL, video and IPv6 are disabled. The framebuffer frontend
has a simpler UI than the GTK frontend. File-backed mmap and SysV SHM are not
available; NetSurf uses its file-read path and XCB PutImage. PutImage transfers
are split to respect the server request limit. No unsupported system call is
emulated as a successful operation.

## Native changes

libnsfb constructors are retained by the linker script and called from a
native main wrapper. The automatic frontend selector now records the chosen
surface type instead of accidentally choosing the last (RAM) surface. XCB
windows get a name/class and standard close protocol. Pointer shapes are
converted to core X11 source/mask cursors; X owns cursor movement, independent
of the browser event loop. Movement no longer uploads framebuffer strips or
plots a second software cursor. Shape uploads occur only when the image or
hotspot changes. Milliseconds are
converted correctly for select; synthetic-event flags are masked before event
dispatch. libnsutils uses the native monotonic clock and its existing seek/read
fallback. NetSurf's platform configuration reflects available libc calls.
Expat uses native urandom plus FD_CLOEXEC; libcurl omits unsupported MSG_NOSIGNAL.

LTO, section collection and the private FreeType driver set keep the stripped
ELF below the current SilliconFS file-size limit, which the builder checks.
The C runtime now supplies the needed formatting, numeric conversion, iconv,
and header definitions. htop uses the shared implementations instead of its
old private duplicates. zlib now also provides its upstream read-only gz API
for the compressed NetSurf Messages resource.

## Verification

```sh
sh src/tests/libos_strftime.sh
sh src/tests/libos_allocator.sh
NETSURF_TEST_INPUT=1 QEMU_ACCEL=tcg QEMU_CPU=max python3 src/tests/tls_qemu.py
```

The disposable VM test installs a native TLS/XCB probe, checks valid and
invalid certificates, starts TinyX/twm/xterm/NetSurf, verifies an HTTPS request,
then checks URL entry after a mouse click and WM_DELETE_WINDOW. Its extra test
CA is placed only in a temporary bundle passed to that test browser. The OS's
trust store is unchanged. Screenshot: `src/build/tls-guest/netsurf.ppm`.
Use `TLS_BROWSER_URL=https://www.google.com/ TLS_PROFILE_SECONDS=20` for an
optional public-page capture, without `NETSURF_TEST_INPUT`.
