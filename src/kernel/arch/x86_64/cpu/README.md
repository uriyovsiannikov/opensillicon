This code prepares x86_64 processors for kernel execution. It sets up the Global Descriptor Table, manages floating-point state, discovers processor topology, and starts secondary CPUs for multiprocessor operation.

Online APs handle TLB shootdowns, synchronous kernel-work broadcasts from the BSP, and CPU-local cooperative kernel threads. A broadcast callback runs concurrently on the BSP and every online AP. It must split shared work by logical CPU, remain non-blocking, leave mappings unchanged, and restore its previous address space before returning. Startup verifies that all online CPUs can load a temporary address space and return to the kernel address space. The BSP owns the process table and can assign ready processes to APs for user-mode execution.

After establishing their local GDT, TSS, GS base and FPU state, APs load
the same interrupt table as the BSP. The AP timer and work IPI have distinct
vectors. The BSP timer owns global ticks. The AP timer ends a local user slice
and returns it to that AP's round-robin queue. Only requests for BSP services
publish a completion and send a reschedule IPI.

Before launching init, the BSP maps a short user program and a separate
user stack for each online AP. Each AP enters ring 3 through its local TSS,
queries the ABI through the normal syscall vector, checks its stack and
SSE state, and returns to its saved kernel continuation. The probe also
checks that a probe with no process cannot invoke process syscalls. The BSP releases the
probe mappings and interrupt stacks after all APs have returned.

AP user resumes pass through the same pending-signal preparation as interrupt
returns. Work IPIs prompt that path without forcing a process migration. The
local AP timer still releases the current user slice, while fatal signal
requests release it with an exit status for teardown by the BSP.

Sleep and nanosleep calls prepare their result on the running CPU and release
an AP with a timer-wait request. A completed wait can resume on an AP without
executing the syscall on the BSP. Global clock updates and wait registration
remain on the BSP. Signal delivery stores a separate aligned FXSAVE image;
sigreturn restores it so a handler's x87/SSE use cannot corrupt the interrupted
user context, including across migration between CPUs.

Each AP owns four user queue slots. The BSP publishes RUNNABLE, the AP claims
ACTIVE, and releases RUNNABLE only after leaving the process address space and
stack. Nonlocal work releases COMPLETE instead. A BSP compare-and-exchange
can claim a RUNNABLE frame for execution on the BSP; it cannot claim ACTIVE.
This ownership transfer also orders FPU and FS-base snapshots across CPUs.

Idle APs can transfer RUNNABLE entries from a peer queue. Both the empty
receiving slot and the source entry are first reserved atomically; publication
of the destination carries the saved frame, FPU image and FS base. No process
access follows destination publication because the BSP can reclaim and reap
that process immediately. The source slot is then cleared. Signals remain in
the process's locked queue and are checked before every user entry, including
when their IPI raced with queue migration.
