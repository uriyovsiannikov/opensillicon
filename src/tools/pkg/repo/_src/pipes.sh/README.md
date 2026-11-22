# pipes.sh

Upstream pipes.sh is included in `upstream/`. The OpenSillicon port is a native
ncurses screensaver with continuously turning pipe segments; press `q` or Esc
to exit. Build with `make -C src/tools/pkg/repo/_src/pipes.sh`.

Terminal support: this static build includes ncurses descriptions for xterm,
xterm-256color, screen, screen-256color, vt100, vt220, linux and dumb. No
external terminfo database is needed. libc supplies xterm when TERM is absent
and preserves an explicitly supplied TERM. New kernels and the rebuilt shell
also preserve the environment across exec; the binaries retain a launch
fallback for older kernels.
