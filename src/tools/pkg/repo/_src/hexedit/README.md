# hexedit

Kevin Pors's `hx` terminal hex editor, adapted for OpenSillicon libos. The
OpenSillicon build omits `SIGWINCH` handling because libos does not expose a
terminal-resize signal. The installed command is named `hexedit`.
