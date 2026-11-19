# Minecraft 1.0.0 checks

Build the Java probes (JDK 8 or newer on the host):

```sh
sh src/tools/pkg/repo/_src/minijvm/opensillicon/tests/build.sh \
    src/build/minijvm-world-check /path/to/client.jar
```

The optional second argument is the original Minecraft 1.0.0 client JAR.
It is not downloaded or included in the repository. Without it, only the
standalone I/O and scheduler probes are built.

Copy `world-io-check.jar` and your client JAR into the guest, then run:

```sh
java -cp /path/to/world-io-check.jar WorldIoProbe
java -cp /path/to/world-io-check.jar SchedulerProbe
java -cp /path/to/world-io-check.jar:/path/to/client.jar MinecraftRegionProbe
```

`MinecraftRegionProbe` uses the game's own region writer/reader to save and
reopen 256 chunks with deterministic payloads in a file larger than 7 MiB.
It removes its fixture `/tmp/minijvm-region-check.mcr` on success. A PASS
requires every chunk header and decompressed payload to match after reopening.

The old live image's 32 MiB reserve could run out after installing TinyX
packages, miniJVM and Minecraft. This caused `.mcr` writes to fail and world
creation to stall at “Saving chunks”. New images reserve 128 MiB for the live
root and 256 MiB for the disk root. These can be configured with
`LIVE_ROOT_RESERVE_MIB` and `ROOT_RESERVE_MIB` when creating images. Existing
filesystems are not resized by these variables.

For the graphical check, start TinyX, launch Minecraft with `-Xmx512m`, create
a world, return to the title screen, and reopen the saved world. Allow enough
time for terrain generation in the interpreted VM and software renderer.

For native thread snapshots, launch Java with `-Dminijvm.trace.threads=true`
and capture stderr to a local file. This prints frame chains
at interpreter safepoints without running Java code on another thread's
runtime. Sampling is disabled by default.
