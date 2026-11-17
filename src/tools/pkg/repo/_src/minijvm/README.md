# miniJVM for OpenSillicon

The interpreter and class library come from
[miniJVM, December 2021](https://github.com/digitalgust/miniJVM/tree/80b52c4dae9e0680ec659ad61e279dd93bf54b65).
The port uses a static native launcher and cooperative Java threads on x86-64.
It runs Java 8 bytecode and uses the miniJVM class library.

Build with `make` in this directory. A host JDK with `javac --release 8`
is required to build the runtime JAR. Install the package with
`doas pkg add minijvm`.

Compile applications on the host with `javac --release 8`. Copy the resulting
classes or a JAR to OpenSillicon and specify the main class explicitly.

```sh
java -version
java -Dexample.setting=value -cp /path/to/classes Example
java -cp /path/to/classes Example
java -cp /path/to/example.jar org.example.Main
```

The bundled class library includes a small AWT implementation for TinyX.
Install `twm`, run `startx`, and launch graphical Java programs from its
xterm. This implementation supports a Frame with one Canvas, the center
region of BorderLayout, window resizing and close requests, and drawing
solid rectangles into the Canvas. BufferedImage supports integer RGB/ARGB
pixels with a shared DataBufferInt raster, rectangle filling, image compositing
and core X11 font text. Color includes HSB conversion.
ImageIO reads PNG files through the existing libpng port.

The package includes an initial LWJGL 2 compatibility JAR at
`/usr/share/minijvm/lwjgl.jar`. Put it first on the classpath of OpenGL
programs that use this port. It supports a software OpenGL context in a
TinyX window or AWT Canvas and presentation with `Display.update()`.
The GL11 bindings cover matrices, client vertex/color/texture arrays,
display lists, texture uploads and updates, pixel readback, depth, alpha
testing and blending. GL13 provides multitexturing. Keyboard and Mouse use
X11 events, including pointer grabbing and wheel input. Game controllers
remain unsupported.

OpenGL uses the classic OSMesa software renderer from
[Mesa 7.8.2](https://archive.mesa3d.org/older-versions/7.x/7.8.2/).
The first graphics build downloads the pinned source archive and verifies
its SHA-256 before compiling the static renderer for libos.

Minecraft 1.0 reaches its main menu using the original client JAR.
QEMU checks also covered the Options and Select World screens.
World generation and loading have not been verified.
Put this port's LWJGL JAR before the original LWJGL 2.9.0 and lwjgl_util
JARs on the classpath. Start `net.minecraft.client.Minecraft` and set
`-Duser.home=/path/to/writable/directory` for its working files.
The QEMU checks use `/tmp` as this directory. The runtime also lacks Java SE networking,
native shared library loading, JIT and JDWP. Audio remains unsupported.
Append-mode file output is unavailable because libos does not implement
O_APPEND. File creation and truncating writes are supported. MessageDigest supplies
incremental MD5, SHA-1 and SHA-256 through the existing mbedTLS port
(Copyright The Mbed TLS Contributors).
The package contains no
Minecraft assets or binaries.

RandomAccessFile supports non-truncating read/write access, seeking, file
length/position queries and extension. Shrinking a file is unavailable in libos.
InflaterInputStream, DeflaterOutputStream and the gzip streams use streaming
zlib compression; corrupt or truncated data raises IOException. The gzip reader
currently handles one member. DateFormat/SimpleDateFormat support numeric
year, month, day, hour, minute and second formatting; parsing is limited to
the ISO pattern `yyyy-MM-dd'T'HH:mm:ss`.

To build the standalone class-library regression check on the host:

```sh
sh opensillicon/tests/build.sh /tmp/minijvm-check
```

Copy `/tmp/minijvm-check/world-io-check.jar` to the target and run
`java -cp /path/to/world-io-check.jar WorldIoProbe`. The check exercises a
recursive Java thread, random-access file preservation, zlib/gzip roundtrips
and independently generated Python fixtures, invalid compressed data, dates,
Random Gaussian reseeding and shared image pixels. It creates and removes
`/tmp/minijvm-world-io-test.dat`.
