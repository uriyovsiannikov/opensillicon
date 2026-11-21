# moon-buggy

Moon-Buggy upstream source, adapted to the OpenSillicon ncurses and libos
build. The game writes its high score to `/tmp/mbscore` (file locking is not
available in libos). Build with
`make -C src/tools/pkg/repo/_src/moon-buggy`.

Terminal support: this static build includes ncurses descriptions for xterm,
xterm-256color, screen, screen-256color, vt100, vt220, linux and dumb. No
external terminfo database is needed. libos supplies xterm when TERM is absent
and preserves an explicitly supplied TERM. New kernels and the rebuilt shell
also preserve the environment across exec; the binaries retain a launch
fallback for older kernels.
