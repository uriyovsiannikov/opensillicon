The I219-V diagnostics entry in the Live ISO loads i219probe.ko for exactly
one PCI Ethernet function with ID 8086:15bc (Cannon Point I219-V (7)).
Connect the Ethernet cable before booting and inspect the i219 lines in
/tmp/btlog.log. The report shows the firmware's current MAC and link state
when the controller exposes them, plus raw STATUS and FWSM registers.

The probe uses the existing PCI claim and BAR sizing checks. Sizing temporarily
changes PCI configuration and restores it before the read-only MMIO mapping.
The probe does not reset the chip, change PHY settings, or program DMA.
It does not provide a network interface. A down link at this stage reflects
the firmware's state and is not a test of driver link negotiation.

References:
- https://github.com/freebsd/freebsd-src/blob/main/sys/dev/e1000/e1000_hw.h
- https://github.com/freebsd/freebsd-src/blob/main/sys/dev/e1000/e1000_regs.h
- https://github.com/freebsd/freebsd-src/blob/main/sys/dev/e1000/e1000_ich8lan.c
