# htop 3.5.3

Install with `pkg add htop` and run `htop` from the console or xterm. Press
F10 or `q` to exit. This OpenSillicon port shows process information and
supports the signals permitted by the kernel; CPU and per-process memory
meters are not implemented. It is statically linked with ncurses 6.5.

Terminal support: this static build includes ncurses descriptions for xterm,
xterm-256color, screen, screen-256color, vt100, vt220, linux and dumb. No
external terminfo database is needed. libc supplies xterm when TERM is absent
and preserves an explicitly supplied TERM. New kernels and the rebuilt shell
also preserve the environment across exec; the binaries retain a launch
fallback for older kernels.
