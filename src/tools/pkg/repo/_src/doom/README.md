# doomgeneric on OpenSillicon

Vendored copy of [doomgeneric](https://github.com/ozkl/doomgeneric) at commit
`e257a09527062bfb986b6e5e0780839b6cbd5b71`, plus a native X11 back-end in
`opensillicon/`.

## Layout

```
doomgeneric/    upstream sources (unmodified except config.h)
opensillicon/   doomgeneric_opensillicon.c - X11 back-end and main()
LICENSE         upstream GPL-2.0
```

The other platform back-ends (`doomgeneric_sdl.c`, `_win.c`, `_allegro.c`,
`_emscripten.c`, `_linuxvt.c`, `_soso*.c`, `_xlib.c`) are kept for upstream
fidelity but are not compiled.

## Local changes

* `doomgeneric/config.h`: defines `HAVE_SYS_STAT_H` and `HAVE_UNISTD_H`, since
  libos provides both headers (upstream's generated config predates them).
* `opensillicon/doomgeneric_opensillicon.c`: new back-end. Differences from the
  upstream Xlib back-end:
  - keys are translated with core `XLookupKeysym` instead of Xkb helpers,
    because the tinyx/kdrive server has no XKB extension;
  - auto-repeat release/press pairs are coalesced, so holding fire/run works
    without detectable auto-repeat support;
  - handles `WM_DELETE_WINDOW`, `DestroyNotify` and `Expose`;
  - sets `WM_CLASS` (`doom`/`Doom`), a fixed-size window and a title;
  - rejects framebuffers that are not 32 bpp little-endian TrueColor.

## Build

```
make src/build/doom/doom                 # 320x200 by default
make DOOM_RESX=640 DOOM_RESY=400 src/build/doom/doom
```

The link needs the shared X11 prefix (`src/build/x11/prefix`), so the target
depends on `src/build/x11/.apps-ready`.

## Run

```
doom -iwad /usr/share/doom/doom.wad
```

The Doom package installs the Doom v1.9 shareware IWAD `doom1.wad` as
`/usr/share/doom/doom.wad`. It contains the first episode only; its MD5 is
`f0cefca49926d00903cf57551d901abe`.
The WAD is from id Software's original `doom95.zip` shareware distribution
(`https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/doom/win95/doom95.zip`).
Build the installable package with `make -C src/tools/pkg/repo/_src/doom`.
Install `twm` first because Doom opens an X11 window. A 4 MiB IWAD fits because SilliconFS
version 2 adds a double-indirect block to the inode map: a file may now span
12 + 1024 + 1024² blocks (about 4 GiB) instead of 1036 blocks (4 MiB).

After installing the `twm` and Doom packages, the twm root menu entry "Doom"
starts exactly that file. `doom` also accepts
the usual `-iwad`, `DOOMWADDIR` and `DOOMWADPATH` lookup.

## Limitations

* No sound: libos has no audio driver yet, so SFX and music are disabled.
* No MIT-SHM on the X server, so every frame is sent with `XPutImage`. At
  320x200x4 that is 256 KB per frame; raise the resolution only if the socket
  path keeps up.
* Single-threaded throughout; no pthread dependency.
