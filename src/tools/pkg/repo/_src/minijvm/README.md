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
java -Xmx512m -cp /path/to/classes Example
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
World generation and entry into a playable world have also been checked in QEMU;
this does not establish real-hardware FPS or save/reopen reliability.
Put this port's LWJGL JAR before the original LWJGL 2.9.0 and lwjgl_util
JARs on the classpath. Start `net.minecraft.client.Minecraft` and set
`-Duser.home=/path/to/writable/directory` for its working files.
The QEMU checks use `/tmp` as this directory. The runtime also lacks Java SE networking,
native shared library loading and JDWP. Audio remains unsupported.
Append-mode file output is unavailable because libos does not implement
O_APPEND. File creation and truncating writes are supported. MessageDigest supplies
incremental MD5, SHA-1 and SHA-256 through the existing mbedTLS port
(Copyright The Mbed TLS Contributors).
The package contains no
Minecraft assets or binaries.

RandomAccessFile supports non-truncating read/write access, seeking, file
length/position queries and extension. Shrinking a file is unavailable in libos.
File.renameTo supports regular files through a copy followed by removal of the
source, since libos has no rename syscall. This operation is not atomic, does
not overwrite an existing destination, and does not support directories.
InflaterInputStream, DeflaterOutputStream and the gzip streams use streaming
zlib compression; corrupt or truncated data raises IOException. The gzip reader
currently handles one member. DateFormat/SimpleDateFormat support numeric
year, month, day, hour, minute and second formatting; parsing is limited to
the ISO pattern `yyyy-MM-dd'T'HH:mm:ss`.

`-Xmx` accepts bytes or a `k`, `m`, `g` suffix, with a minimum of 1 MiB.
It sets the interpreter's heap collection threshold (200 MiB by default),
also reported by Runtime.maxMemory. It is not a hard process memory limit;
live objects and native graphics allocations can exceed it.

To build the standalone class-library regression check on the host:

```sh
sh opensillicon/tests/build.sh /tmp/minijvm-check
```

Copy `/tmp/minijvm-check/world-io-check.jar` to the target and run
`java -cp /path/to/world-io-check.jar WorldIoProbe`. The check exercises a
recursive Java thread, random-access file preservation, zlib/gzip roundtrips
and independently generated Python fixtures, invalid compressed data, dates,
Random Gaussian reseeding and shared image pixels. It creates and removes
`/tmp/minijvm-world-io-test.dat`, `/tmp/minijvm-rename-test.dat` and
`/tmp/minijvm-rename-moved.dat`.
Run `java -cp /path/to/world-io-check.jar SchedulerProbe` to check that two
busy Java threads both make progress through cooperative loop safepoints.

## JIT on OpenSillicon

The x86-64 JIT uses the SLJIT sources from the same pinned upstream miniJVM
revision (`80b52c4dae9e0680ec659ad61e279dd93bf54b65`), restored under
`vendor/utils/sljit`. Their redistribution license is retained in each file.
The local allocator uses separate RW and RX phases; RWX mappings are rejected
by the kernel. Mapping validation, fork and unmap preserve these permissions.

A kernel with private RX mappings is required. On an older kernel Java prints
`kernel lacks JIT memory support; using interpreter` and continues without JIT.
`java -Xint ...` explicitly selects the interpreter for comparison. Startup
prints the execution mode, and shutdown reports the compiled method count.
Cooperative scheduling and GC safepoints remain active in compiled loops.

Build and run the native boot checks with KVM, OVMF, a host JDK and xorriso:

```sh
make kernel src/build/minijvm/java src/build/minijvm/minijvm_rt.jar src/build/populate_root
python3 src/tools/checks/minijvm_jit.py
```

The check boots a separate image under `src/build/jit-check`, verifies RW/RX,
RWX rejection, fork and unmap, then runs both JVM execution modes. It compares
Minecraft 1.0.0's actual Perlin/octave noise output with the host JDK and logs
all results to `src/build/jit-check/serial.log`. It does not measure complete
world creation or graphical FPS.

Lightweight Minecraft profiling is enabled by `-Dopensillicon.profile=1`. Reports
are written to `/tmp/minijvm-profile.log` so drawing the terminal does not slow
the game. `-Dopensillicon.profile.log=stderr` selects the console instead. The
profiler accounts exclusive fiber wall time: combined Java/VM regions, native
methods, allocator, I/O and scheduler. Nested Java calls do not each take a
timestamp. `-Dopensillicon.profile.methods=1` (or environment variable
`OPEN_SILLICON_JVM_PROFILE_METHODS=1`) opts into expensive full method tracing
and the VM/JIT/interpreter split. Full tracing can slow call-heavy code many
times; do not use it to assess FPS.
Invariant-TSC profiling avoids a clock syscall at each OpenGL/native boundary;
the monotonic fallback has higher overhead. No profiling is enabled by default.

`MINIJVM_CHECK_PROFILE=1 python3 src/tools/checks/minijvm_jit.py` runs the
arithmetic/array/thread probe in both modes and checks that exclusive category
totals match the measured interval. QEMU exposes invariant TSC for this check.
`python3 src/tools/checks/libos_allocator.py` checks allocation data, alignment,
reallocation, overflow, calloc and fork behavior in the native kernel.

JAR resources use direct metadata lookup. Opening a ZipFile no longer creates
Java entries for every file; enumeration is materialized only when requested.
`python3 src/tools/checks/minijvm_zip.py` checks missing, empty and directory
entries, sizes, repeated enumeration and resource contents. Set
`MINIJVM_ZIP_BASELINE_RT=/path/to/older-runtime.jar` to compare lookup throughput
using the same JVM and a generated 1500-file fixture.

Primitive boxing reuses Byte objects, Long values -128..127 and Short values
-128..512 (including Minecraft's default entity air-supply value 300).
StringBuilder appends integer digits directly without temporary strings.

Primitive RandomAccessFile writes use one native transition and descriptor
write per 2/4/8-byte value, preserving big-endian output and immediate visibility.
`python3 src/tools/checks/minijvm_file.py` checks native file semantics.
`python3 src/tools/checks/minijvm_getters.py` checks leaf getters, virtual
dispatch, primitive widths, references, synchronization and volatile fallback.
Resolved, compiled non-volatile leaf getters bypass callee-frame setup. Null
receivers and debugger sessions retain the ordinary execution path.
Periodic Java backedge yields use a passively calibrated 1 ms invariant-TSC
quantum when supported; explicit yield and blocking waits stay immediate.
