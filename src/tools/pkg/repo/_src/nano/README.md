# GNU nano 9.3

Install with `pkg add nano` and run `nano [file]` from a shell. The binary is
statically linked with ncurses 6.5. This OpenSillicon build is byte-oriented;
see the upstream documentation for GNU nano's general behavior and license.

Terminal support: this static build includes ncurses descriptions for xterm,
xterm-256color, screen, screen-256color, vt100, vt220, linux and dumb. No
external terminfo database is needed. libc supplies xterm when TERM is absent
and preserves an explicitly supplied TERM. New kernels and the rebuilt shell
also preserve the environment across exec; the binaries retain a launch
fallback for older kernels.
