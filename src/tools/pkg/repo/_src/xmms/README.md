# XMMS 1.2.11 for OpenSillicon

The original GTK+ 1.2 XMMS interface, statically linked with GLib 1.2.10,
GTK+ 1.2.10, WAV and MPG123 input plugins and native `/dev/audio` output.
All player and toolkit sources are kept here, with origins and checksums in
SOURCES. OpenSillicon changes are local modifications, not upstream releases.

Install with `doas pkg add xmms` and run `xmms /path/to/music.mp3` inside a
TinyX desktop (`startx`). WAV and MP3 files are supported; extra dynamically
loaded plugins, CD audio and network streaming are not supported by this port.
The kernel must provide the continuous PCM `/dev/audio` interface. Audio
currently targets Intel HDA with a Realtek ALC235 rear green analogue output.
Pause lets the already queued audio finish (up to about one second); then the
playback clock stays paused.
Playback does not require doas for the normal desktop user in group 1000.

Pause stops queuing new audio; the already buffered tail (under one second)
finishes playing. Stereo balance and volume are supported.

Output converts supported PCM formats/rates to 48 kHz stereo signed 16-bit.
The original decoder/UI threads run as cooperative fibers until native user
threads are available. The port uses the built-in skin. X fonts fall back to
`fixed` when the requested Helvetica font is unavailable.

Build from the repository root with `make -C src/tools/pkg/repo/_src/xmms`.
Build products stay in src/build/xmms. The package includes the adapted XMMS,
GLib and GTK+ sources under /usr/share/doc/xmms/sources.tar.gz; platform libc,
X11 and kernel sources are in the OpenSillicon source tree. See COPYING files
for the GPL/LGPL terms. tinycthread.h retains its original zlib-style license;
its local fiber implementation is adapted from the OpenSillicon miniJVM port.
