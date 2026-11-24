<div align="center">
  <img src="other/logo.png" alt="logo" width="88">
  <h1>opensillicon</h1>
  <p>a small x86_64 operating system built from the ground up.</p>
  <img src="other/screenshot2.png" alt="shell" width="620">
</div>

opensillicon is an experimental unix-like operating system with a modular
monolithic kernel and a native userspace.

## architecture

```
hardware - uefi - smp - pci - disk - nic - hda - gpu - usb
    |
    v
+----------------------------------------------------------------+
| kernel                                                         |
| boot/mm/sched / process/ipc / vfs / ctcp / drivers             |
+----------------------------------------------------------------+
    | syscalls - sysctl - /dev
    v
+----------------------------------------------------------------+
| userspace                                                      |
| init/svctl / sh / coreutils / pkg                              |
+----------------------------------------------------------------+
```

## build and run

on a linux x86_64 host, install gcc, gnu binutils, gnu make, py3, qemu-x86 and `xorriso`, then run:

```sh
make
make root      # build the root filesystem
make run-gui   # graphical display
```

## source map

- `src/kernel/` - kernel, architecture code, drivers, filesystems, and networking.
- `src/user/` - init, shell, utilities, musl under `thirdparty/musl/`, and xorg components.
- `src/include/` - kernel and userspace abi headers.
- `src/tools/` - image and filesystem build tools.
- `src/build/` - generated build artifacts.

## license

project code is available under the [bsd 2-clause license](LICENSE). vendored
and third-party components retain their respective licenses.
