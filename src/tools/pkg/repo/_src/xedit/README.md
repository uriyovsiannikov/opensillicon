# xedit

X.Org xedit 1.2.4, built with OpenSillicon's libc and static Xaw/Xt client
libraries. Bundles the editor's Lisp modules under `/usr/share/xedit/lisp`.
Lisp shell commands use libc `execve` and inherit the editor's environment.
The port omits process accounting timers because the OS does not provide
interval timers or process CPU accounting.
