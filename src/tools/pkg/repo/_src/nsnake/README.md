# nsnake

OpenSillicon native C/ncurses port providing a simple Snake game with arrow
keys or WASD. Upstream nsnake source remains in `upstream/`; the original
implementation uses C++ standard-library features unavailable in libos.

Terminal support: this static build includes ncurses descriptions for xterm,
xterm-256color, screen, screen-256color, vt100, vt220, linux and dumb. No
external terminfo database is needed. libos supplies xterm when TERM is absent
and preserves an explicitly supplied TERM. New kernels and the rebuilt shell
also preserve the environment across exec; the binaries retain a launch
fallback for older kernels.
