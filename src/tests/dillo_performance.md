# Dillo page loading profile

Historical measurements from the Dillo port. The current `tls_qemu.py` harness
launches NetSurf XCB instead.

A 40-second QMP instruction-pointer sample of both vCPUs under QEMU TCG
(`-cpu max -smp 2`) loading `https://www.google.com/` identified allocation,
not cryptography, as the dominant hot path on 2026-11-14.

| Build | All vCPU samples | Kernel heap traversal | e1000 link-status MMIO read |
| --- | ---: | ---: | ---: |
| Before changes | 1966 | 657 (33.4%) | 260 (13.2%) |
| Small-block allocator | 1968 | 0 | 250 (12.7%) |
| Allocator + shared, rate-limited link polling | 1970 | 0 | 0 |

These are sampled instruction locations, including idle vCPUs, not exact CPU
accounting or an end-to-end speed ratio. The page is remote and can change.
The initial screenshot still showed `Images 0 of 2`; the final screenshot
showed `Images 2 of 2` and the Google logo in the same observation window.
No KVM acceleration was used to obtain these results.

## Causes and changes

* Native libos previously called mmap/munmap for every allocation, including
  tiny ones. Each mapping also allocated kernel metadata. The kernel heap
  scans blocks linearly while holding an IRQ-disabling lock, magnifying both
  memory overhead and input/scheduling latency. Libos now pools small blocks,
  releases excess empty pages, retains at most seven spare pages per process,
  and resizes allocations in place when capacity permits. The kernel heap
  algorithm is unchanged.
* Both `cyclone_net_poll` and `udp_socket_poll` independently queried the NIC
  link on every network safe point. Reading the emulated MMIO register is
  expensive. Cyclone now checks link state at most once per 100 ms, and UDP
  consumes that state. Packet reception is still polled on every safe point;
  only cable-state detection has the 100 ms polling interval.

## Profiling the current browser

From the repository root:

```sh
sh src/tests/libos_allocator.sh
TLS_PROFILE_SECONDS=40 TLS_BROWSER_URL=https://www.google.com/ \
  QEMU_ACCEL=tcg QEMU_CPU=max python3 src/tests/tls_qemu.py
```

The harness first checks valid TLS, certificate rejection and native entropy,
installs the `twm` and NetSurf packages, then starts TinyX, twm, xterm and
NetSurf in a disposable live VM. It writes
`src/build/tls-guest/profile.json` (elapsed seconds and HMP register snapshots)
and `src/build/tls-guest/netsurf.ppm`. Resolve kernel RIP values using
`addr2line -f -e src/build/kernel.elf ADDRESS`. Module addresses must be resolved
against the loaded module's text base and ELF (the observed e1000 MMIO read was
at text offset `0x807`). Keep the matching ELFs when comparing builds.

Omit `TLS_BROWSER_URL` to test the local HTTPS fixture with an asserted browser
request instead of relying on an external site. The allocator host test checks
mixed sizes, data preservation, alignment, overflow, failed realloc, page
reclamation and repeated reuse. Its 10,000 simultaneous 24-byte allocations
need 159 mappings rather than 10,000.
