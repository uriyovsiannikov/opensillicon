# hexedit

Kevin Pors's `hx` terminal hex editor, adapted for OpenSillicon libc. The
OpenSillicon build omits `SIGWINCH` handling because libc does not expose a
terminal-resize signal. The installed command is named `hexedit`.
