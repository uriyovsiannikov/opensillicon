The kernel builds Eclipse ThreadX USBX 6.5.2 as a standalone host stack.
The upstream tag, commit and license are recorded in usbx/UPSTREAM.
OpenSillicon supplies the x86-64 port and the xHCI host controller backend.
USBX registers Hub, HID keyboard and mouse, and Storage classes.

On boot the BSP claims up to four xHCI PCI functions, takes ownership from
firmware, and sets up command and event rings and device contexts using the
kernel DMA API. It verifies command and event ring wraparound before handing
the controller to USBX. A failed attachment releases resources after the
controller has been reset and bus mastering has been disabled.
If firmware keeps BIOS ownership for one second, the backend reclaims the
legacy semaphore, disables legacy SMIs, and verifies OS ownership before
resetting the controller. Unreadable registers still abort attachment.
Run `sh src/tests/xhci_ownership.sh` for the simulated handoff checks.

USBX runs at BSP syscall and scheduler idle safe points. It polls root ports,
enumerates USB 2 devices, and manages their logical USB addresses. The xHCI
backend translates Set Address into an Address Device command and handles
control transfers through endpoint zero. Devices on separate root ports have
separate DMA workspaces. Disconnect releases the device slot before the
workspace can be reused.

Root-port USB 2 HID devices can use interrupt IN and OUT endpoints.
Each endpoint has its own DMA allocation and transfer state, so composite
devices can expose multiple HID interfaces. DMA allocations are released
after a successful endpoint drop or Disable Slot command.
USBX decodes keyboard reports into Set 1 press and release events for the
console and the existing keyboard syscall. Relative mouse movement and
three buttons use the existing mouse syscall. The mouse ABI has no wheel
field. HID interrupt polling and controller event completion run on the BSP.

USB storage data still requires bulk endpoints. External hubs, SuperSpeed
devices, and isochronous transfers require additional controller support.
The imported EHCI entry requires the threaded USBX mode.

Run `sh src/tests/usbx_qemu.sh` to build the ISO and test xHCI enumeration,
keyboard disconnect and reconnect, and HID input without a PS/2 controller.
The input test exercises transfer ring wraparound, a cursor key, a shifted
character in a console read, and relative mouse movement and button changes.
The fixture also checks boot with no USB controller and with EHCI present.

After six consecutive raw console read timeouts with no key events, the console
prints input diagnostics once. They include ready USB keyboard count, the latest
USBX error, root-port registers, command/control completion codes, and interrupt
report counts. This helps diagnose hardware where a host attaches but input
never arrives.
