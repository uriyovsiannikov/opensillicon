The scheduler maintains CPU-local cooperative kernel-thread queues. Threads can be pinned to a logical CPU; queue insertion is synchronized and wakes a sleeping AP with an IPI. AP scheduling starts after the heap and BSP scheduler are ready. The BSP assigns ready user processes to idle APs for time slices, including immediately after fork. Candidate selection rotates through the process list so busy processes cannot monopolize AP assignments. APs send a reschedule IPI after releasing a process. This wakes a sleeping BSP or preempts its current user process. The periodic BSP timer also collects completions. APs handle ABI version, PID, credentials, monotonic time, FS base private-memory map/protect/unmap and shared-memory create/detach calls locally. A running process owns its user page tables until it returns its slice. For other syscalls, exceptions and timer preemption, APs return the saved frame, FPU state and FS base. The BSP handles those syscalls and exceptions, delivers signals, and manages process lifetime. A deliverable signal for a process running on an AP requests an early end to its slice through an IPI. The AP also checks that request before entering user mode and on its periodic timer. AP heap allocations can extend the kernel heap through synchronized kernel page-table updates and TLB shootdowns; address-space creation/destruction remain BSP-owned.

`make smp-scheduler-qemu-test` builds a separate kernel with
`KERNEL_TEST_SMP_SCHEDULER`. It runs two pinned threads on each online CPU,
checks their CPU and address space through repeated yields, exercises heap
allocation on APs, and verifies that exiting threads leave only the bootstrap
thread and no unreaped stacks. Invalid and offline CPU targets are rejected.
The production kernel does not run this fixture during normal startup.

Each test thread also submits a child to another online AP (or its own CPU
when no other AP exists). Children check CPU affinity, yield, allocate and free
heap memory, and exit before the fixture checks queue cleanup. This exercises
concurrent producers on remote queues. `make smp-scheduler-partial-qemu-test`
injects a startup failure on logical CPU 1 and verifies that submissions skip
that CPU while surviving queues continue to run.
