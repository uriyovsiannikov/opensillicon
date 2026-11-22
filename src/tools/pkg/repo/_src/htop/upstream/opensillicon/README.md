# htop on OpenSillicon

This directory adapts upstream htop 3.5.3 to libc and the static ncurses
library shared with Nano. Upstream sources and GPLv2+ license are included
in the parent directory.

Build with `make -C src/tools/pkg/repo/_src/htop`. The `htop` package installs
it as `/usr/bin/htop`. Run it from the console or xterm and press F10 or q to
exit.

The process table shows PID, parent PID, effective user, state, and the
executable basename. The header shows physical memory, process counts,
and uptime. F9 can send signals allowed by the kernel. This port does not
provide CPU utilization, memory usage per process, or persistent htop
settings.
