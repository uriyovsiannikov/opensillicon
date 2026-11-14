The block layer provides read and write access to block devices for filesystem code. This directory also connects SilliconFS operations to block-backed storage.

AHCI disk discovery runs during PCI enumeration before selecting the root
filesystem. It scans SATA ports on up to eight AHCI controllers and registers
up to seven disks as ahci0 through ahci6, each with independent command and DMA
storage. One registry slot remains available for the Live RAM disk ram0.

Live boot mounts ram0 and exposes detected SATA disks through /proc/disks and
blkconf without mounting or formatting them. Disk boot selects the first detected
disk containing a valid whole-disk SilliconFS. Device numbering follows PCI and
port discovery order; it is not a persistent identity. GPT/MBR partitions, NVMe,
and USB mass-storage block I/O are not implemented here.
