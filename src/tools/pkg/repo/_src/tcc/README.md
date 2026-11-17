# TinyCC for OpenSillicon

The `tinycc` directory comes from the
[TinyCC 0.9.27 release archive](https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27.tar.bz2).
Its SHA-256 is `de23af78fca90ce32dff2dd45b3432b2334740bb9bb7b05bf60fdbfc396ceb9c`.

Run `make` in this directory to build `tcc-0.9.27.spkg`. The compiler
defaults to static x86-64 ELF output and uses the startup object, libos,
and TinyCC runtime in `/usr/lib/tcc/lib`. The bundled headers are in
`/usr/lib/tcc/include`. A different toolchain directory can be selected
with `-B`.

The port has no shared-library output, thread-local storage, or in-process
`-run` support. Decimal long-double literals are parsed through the libos
double-precision conversion routine.
