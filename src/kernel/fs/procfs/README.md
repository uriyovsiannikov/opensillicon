procfs presents selected kernel and process information through filesystem entries. Programs can inspect this information using the normal filesystem access path.

`/proc/netpci` lists PCI class 02 network controllers. Run `netconf` to
collect the PCI address, vendor and device IDs, revision, subsystem IDs,
command register, interrupt line/pin and assigned BARs. Numeric fields are
hexadecimal. The report only reads PCI configuration space, so it can identify
controllers before a network driver is available. It does not enumerate USB
network adapters.

`/proc/netif` contains the interface name registered with the IPv4 stack,
or is empty when no interface is registered. `ifconfig` uses it for the label.
