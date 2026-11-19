# Minecraft 1.0.0 for OpenSillicon

Install `minijvm` and this `minecraft` package, start X11, then run `minecraft`.
World saves are stored under `$HOME/.minecraft/saves`.

The package contains the Minecraft 1.0.0 client JAR and its LWJGL 2.9.0 Java
support libraries. Minecraft's client JAR is proprietary; this archive is for
the owner's personal use. Do not publish it to a public package repository or
redistribute it without authorization from the rights holder.

The client was verified with 1 GiB of RAM in QEMU. World creation, rendering,
and saving worked; reopening a saved world has not yet been verified.
