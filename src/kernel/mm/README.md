Memory management handles allocation and mapping of physical and virtual memory. It also provides the kernel heap, shared memory, and DMA memory and ring support for devices.

Private anonymous memory operations may run on the AP that owns the process.
Only that process can change its private mappings, and it cannot run on two
CPUs at once. AP validation uses the active address space without traversing
the BSP-owned address-space list. Shared-memory creation and detach also run on the owning CPU. Mapping lists
belong to each process, while inherited SHM objects use atomic reference counts
so concurrent detach and process exit free the backing pages exactly once.
Address-space creation, cloning and destruction run on the BSP. Physical pages and heap
allocations use their existing locks, and permission changes issue TLB
shootdowns before returning.
