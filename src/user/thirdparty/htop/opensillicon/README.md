# htop on OpenSillicon

This directory adapts upstream htop 3.5.3 to libos and the static ncurses
library shared with Nano. Upstream sources and GPLv2+ license are included
in the parent directory.

Build with `make src/build/htop/htop`. The root image installs it as
`/bin/htop`. Run `htop` from the console or xterm and press F10 or q to exit.

The process table shows PID, parent PID, effective user, state, and the
executable basename. The header shows physical memory, process counts,
and uptime. F9 can send signals allowed by the kernel. This port does not
provide CPU utilization, memory usage per process, or persistent htop
settings.
