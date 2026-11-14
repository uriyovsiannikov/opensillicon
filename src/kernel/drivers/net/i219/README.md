The I219-V diagnostics entry in the Live ISO loads i219probe.ko for exactly
one PCI Ethernet function with ID 8086:15bc (Cannon Point I219-V (7)).
Connect the Ethernet cable before booting and inspect the i219 lines in
/tmp/btlog.log. The report shows the firmware's current MAC and link state
when the controller exposes them, plus raw STATUS and FWSM registers. It also
reads the PHY ID and basic status through MDIO after acquiring software ownership.

The probe uses the existing PCI claim and BAR sizing checks. Sizing temporarily
changes PCI configuration and restores it before mapping MMIO. The probe
writes the software ownership flag and MDIO read commands, then releases
ownership. It does not reset the chip, change PHY settings, or program DMA.
It does not provide a network interface. A down link at this stage reflects
the firmware's state and is not a test of driver link negotiation.

A separate Ethernet test entry loads i219.ko. It uses the firmware-configured
PHY and receive address, drains the MAC, programs the existing e1000 DMA rings
for legacy descriptors, then attaches CycloneTCP for DHCP. It requires
identity DMA and a validated BAR. Keep the Ethernet cable connected before
boot. After boot, use cat /tmp/btlog.log for the i219 lines and ifconfig for
the assigned address. A successful DHCP exchange exercises both TX and RX.
The diagnostics entry remains available when this experiment fails.

Hardware results for PCI 8086:15bc rev 0x10, subsystem 17aa:36eb:
the probe read PHY ID 0x015400a6 with link up. The Ethernet test acquired
an IPv4 DHCP lease and received an ICMP echo reply from the LAN gateway.
This covers the firmware-initialized link on that machine; initialization
from a down PHY and cable reconnect remain to be verified.

References:
- https://github.com/freebsd/freebsd-src/blob/main/sys/dev/e1000/e1000_hw.h
- https://github.com/freebsd/freebsd-src/blob/main/sys/dev/e1000/e1000_regs.h
- https://github.com/freebsd/freebsd-src/blob/main/sys/dev/e1000/e1000_ich8lan.c
- https://github.com/freebsd/freebsd-src/blob/main/sys/dev/e1000/e1000_phy.c
