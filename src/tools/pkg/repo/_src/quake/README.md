# Quake for OpenSillicon

This is the C software renderer and local single-player engine from
[id Software's GPL Quake release](https://github.com/id-Software/Quake/tree/bf4ac424ce754894ac8f1dae6a3981954bc9852d/WinQuake).
It presents a 480×360 window in TinyX with keyboard and mouse support.
The port uses the null audio and local network drivers.
Its format adapter uses [stb_sprintf](https://github.com/nothings/stb_sprintf)
for the floating-point fields in game settings and saves.

Run `make` here to build `quake-1.09.spkg`. Install `twm` and `quake`
through `pkg`, then run `startx`.

Provide `id1/pak0.pak` from Quake shareware or retail Quake, and optionally
`id1/pak1.pak` from retail Quake. The original
[Quake 1.06 shareware archive](https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/quake/quake106.zip)
contains a self-extracting LHA archive named `resource.1`. Extract it on the
build host with `7z x resource.1` and copy `ID1/PAK0.PAK` to the guest
using the lowercase name `id1/pak0.pak`.

From an X terminal, run `quake -basedir /path/to/quake-data` or start
`quake` in the directory containing `id1`. The default system data path
is `/usr/share/quake`. Use a writable data directory for configuration
and saves. The package contains the engine, with game data supplied separately.
