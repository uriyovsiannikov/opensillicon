xclock is the classic X Window System clock utility.  It displays
the time in analog or digital form, continuously updated at a
frequency which may be specified by the user.

## OpenSillicon

This port uses the sources from
[X.Org xclock 1.2.0](https://xorg.freedesktop.org/archive/individual/app/xclock-1.2.0.tar.xz).
Build the package with `make` in this directory. It links statically against
libos and the X11 libraries built for the `twm` package. The clock uses core
X11 drawing with TinyX and displays the UTC time supplied by libos.

Install `twm` and `xclock`, run `startx`, then launch `xclock` or
`xclock -digital` from the terminal.

All questions regarding this software should be directed at the
Xorg mailing list:

  https://lists.x.org/mailman/listinfo/xorg

The primary development code repository can be found at:

  https://gitlab.freedesktop.org/xorg/app/xclock

Please submit bug reports and requests to merge patches there.

For patch submission instructions, see:

  https://www.x.org/wiki/Development/Documentation/SubmittingPatches

