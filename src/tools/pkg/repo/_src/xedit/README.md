# xedit

X.Org xedit 1.2.4, built with OpenSillicon's libos and static Xaw/Xt client
libraries. Bundles the editor's Lisp modules under `/usr/share/xedit/lisp`.
The port omits process accounting timers and uses `execv` for Lisp shell
commands because libos does not provide `setitimer` or `execve`.
