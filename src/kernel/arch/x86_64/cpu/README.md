This code prepares x86_64 processors for kernel execution. It sets up the Global Descriptor Table, manages floating-point state, discovers processor topology, and starts secondary CPUs for multiprocessor operation.

Online APs handle TLB shootdowns, synchronous kernel-work broadcasts from the BSP, and CPU-local cooperative kernel threads. A broadcast callback runs concurrently on the BSP and every online AP. It must split shared work by logical CPU, remain non-blocking, leave mappings unchanged, and restore its previous address space before returning. Startup verifies that all online CPUs can load a temporary address space and return to the kernel address space. The BSP owns the process table and can assign ready processes to APs for user-mode execution.

After establishing their local GDT, TSS, GS base and FPU state, APs load
the same interrupt table as the BSP. The AP timer and work IPI have distinct
vectors. The BSP timer owns global ticks. The AP timer ends local user slices.

Before launching init, the BSP maps a short user program and a separate
user stack for each online AP. Each AP enters ring 3 through its local TSS,
queries the ABI through the normal syscall vector, checks its stack and
SSE state, and returns to its saved kernel continuation. The probe also
checks that a probe with no process cannot invoke process syscalls. The BSP releases the
probe mappings and interrupt stacks after all APs have returned.
