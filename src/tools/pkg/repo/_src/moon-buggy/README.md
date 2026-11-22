# moon-buggy

Moon-Buggy upstream source, adapted to the OpenSillicon ncurses and libc
build. The game uses musl's signal and time functions directly. It writes its
high score to `/tmp/mbscore` (native file locking is not implemented). Build with
`make -C src/tools/pkg/repo/_src/moon-buggy`.

Terminal support: this static build includes ncurses descriptions for xterm,
xterm-256color, screen, screen-256color, vt100, vt220, linux and dumb. No
external terminfo database is needed. The shell supplies xterm when TERM is
absent and preserves an explicitly supplied TERM across exec.
