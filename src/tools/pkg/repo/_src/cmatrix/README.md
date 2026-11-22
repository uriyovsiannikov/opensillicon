# cmatrix

Upstream CMatrix 2.0, adapted to OpenSillicon's libc and ncurses build.
Package recipe: `make -C src/tools/pkg/repo/_src/cmatrix`.

Terminal support: this static build includes ncurses descriptions for xterm,
xterm-256color, screen, screen-256color, vt100, vt220, linux and dumb. No
external terminfo database is needed. libc supplies xterm when TERM is absent
and preserves an explicitly supplied TERM. New kernels and the rebuilt shell
also preserve the environment across exec; the binaries retain a launch
fallback for older kernels.
