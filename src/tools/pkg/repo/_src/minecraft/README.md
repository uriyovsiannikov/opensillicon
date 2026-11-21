# Minecraft 1.0.0 for OpenSillicon

Install `minijvm` and this `minecraft` package, start X11, then run `minecraft`.
World saves are stored under `$HOME/.minecraft/saves`.
Startup failures are printed to the terminal and saved to
`$HOME/.minecraft/crash.log`; the desktop adapter avoids the unsupported AWT
crash dialog.

The package contains the Minecraft 1.0.0 client JAR and its LWJGL 2.9.0 Java
support libraries. Minecraft's client JAR is proprietary; this archive is for
the owner's personal use. Do not publish it to a public package repository or
redistribute it without authorization from the rights holder.

The client was verified with 1 GiB of RAM in QEMU. World creation, rendering,
and saving worked; reopening a saved world has not yet been verified.

For performance diagnostics, run `minecraft --profile`. miniJVM writes reports
to `/tmp/minijvm-profile.log` so drawing terminal text does not affect the game.
Use `tail -n 30 /tmp/minijvm-profile.log` to read a recent interval. Each report
contains `[MC profile]`, `[MC detail]`, and the hottest native `[MC method]` entries.
Times are exclusive wall time across cooperative fibers: GC and other fibers
are not counted simultaneously. They include kernel waits and OS preemption,
so these are not CPU utilization percentages.

`GL_ms` measures software OpenGL work; `X11_ms` is divided into image scanning,
sending and copying. It includes client-side backpressure but cannot measure
all asynchronous X-server rendering. `java_ms` combines Java execution and VM
call setup. Native methods, allocator, file operations, input, scheduler, idle
sleeps and compilation are separate. Nested Java calls do not each read the
clock or update a method table in this lightweight mode.

For short developer investigations, setting the environment variable
`OPEN_SILLICON_JVM_PROFILE_METHODS=1` enables the former full Java method trace.
It additionally splits interpreter/JIT/VM bridge time. It can slow call-heavy
code many times and must not be used to measure normal FPS. Method `self_ms`
excludes callees; `[MC callers]` samples recent callers, not a complete graph.

On CPUs advertising invariant TSC the profiler reads the counter directly and
scales it to each interval's monotonic duration. Other CPUs use the higher-cost
monotonic fallback. Profiling still adds overhead: use an unprofiled launch to
assess normal play. Discard startup reports. Output requires matching miniJVM.

With a JIT-capable kernel and current miniJVM, the default launch uses JIT.
For comparison on the same machine:

```sh
minecraft --profile
minecraft --interpreter --profile
```

Both options must precede any Minecraft arguments. On an older kernel Java
reports the interpreter fallback; a package update alone cannot enable JIT.

With the software renderer, choose a small render distance in Video Settings.
Minecraft 1.0.0 stores it as `viewDistance` (3 is Tiny); `renderDistance` is not
the options-file key in this version. Existing user settings are preserved.
