The scheduler maintains CPU-local cooperative kernel-thread queues. Threads
can be pinned to a logical CPU; synchronized queue insertion wakes a sleeping
AP with an IPI. AP scheduling starts after the heap and BSP scheduler are ready.

The BSP fills each AP's bounded user-process queue, preferring the least
occupied online CPU. Global candidate selection rotates through the process
list. Each AP selects its own queued process in round-robin order. An AP timer
saves the current frame, FPU state and FS base and requeues the process locally;
it does not return the quantum to the BSP or send a completion IPI. This lets
CPU-bound processes make progress through repeated quanta without BSP dispatch.
The BSP can atomically reclaim a queued process when it needs work or another
CPU has excess runnable work. It leaves the sole runnable process on an AP when
it already has work of its own. An AP with no local runnable work can take an
inactive queued process from another online AP. It prefers a victim with more
than one runnable/active process, avoiding a sole process bouncing between idle
CPUs. Victim selection rotates; idle timer wakeups bound the retry interval.

Slot states publish exclusive ownership: EMPTY -> RESERVED -> RUNNABLE ->
ACTIVE, then RUNNABLE for another local quantum or COMPLETE for BSP service. The AP returns
to the kernel address space and leaves the process stack before either release.
Producers reserve an EMPTY destination with a compare-and-exchange. Migration
also reserves the RUNNABLE source, publishes the destination, then clears the
source. The process stays RUNNING in the global table throughout the transfer.
Slot pointers use atomics so signal lookup can safely race with removal/reuse.
The BSP likewise reserves RUNNABLE before reclaiming it or collects COMPLETE;
no CPU can claim ACTIVE or another producer's RESERVED slot. Queued processes remain reserved as RUNNING in the
BSP-owned process table. Deferred syscalls, faults, exits and wait requests send
a completion IPI; the BSP timer is the fallback. Signals also check queued
owners and are inspected on every return to userspace. Progress diagnostics
use atomics because an AP can execute another slot during BSP collection.

APs handle ABI version, PID, credentials, monotonic time, FS base, private-memory
map/protect/unmap, shared-memory create/detach, signal action, signal mask,
signal return, sleep and nanosleep calls locally. Ready pipe I/O, including
readv/writev, pipe creation and endpoint close also run locally. Signal queues,
masks and action pairs share an IRQ-safe lock across BSP senders and AP callers.
A running process owns its user page tables until it releases its slot.
Other syscalls and exceptions return to the BSP, which manages process lifetime.
Fatal signals record a private exit request; only the BSP publishes exit status
in the process table and performs teardown and parent notification.

Pipe operations that would block release the AP with a local-wait request. The
BSP registers it after collecting the process, while a readiness generation
prevents lost notifications before registration. Woken local waits keep the
original syscall frame and can continue on APs. Legacy BSP waits still retry
a user interrupt. Signals cancel a local wait with EINTR before handler
delivery; SIGKILL follows normal teardown. AP channel notifications are
coalesced without retaining channel pointers. Scheduler safe points and
user-mode reschedule IPIs drain them and retry channel waiters.

AP timer waits publish their deadline after releasing the process. The BSP
registers the wait, handles deadlines elapsed before collection, and wakes
expired waits. The process can return directly to userspace on an AP. Global
clock ticks and process-list updates remain BSP-owned. An AP preparing EINTR
keeps its process RUNNING until release; only the BSP publishes READY or BLOCKED.

Signal delivery saves an aligned FXSAVE snapshot so sigreturn restores the
interrupted x87/SSE state as well as general registers. The saved user context
returns through the current kernel-stack frame; interrupt continuations must
never run on a frame embedded in the heap process object. AP heap allocations
can extend the kernel heap through synchronized kernel page-table updates and
TLB shootdowns; address-space creation/destruction remain BSP-owned.

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

`sh src/tests/smp_user.sh` builds disposable guest images and checks private
mappings, shared-object lifetime, masked and IPI signal delivery, handler
return, invalid signal contexts, unmaskable SIGKILL and concurrent pipe I/O
on 1, 2 and 8 CPUs. Timer tests check duration, zero and invalid waits, EINTR
remainder including an aliased request buffer, SIGKILL, x87/SSE preservation
and 7000 signal attempts racing with seven sleepers. Each fixture runs as init and must finish with status zero. On multiple
CPUs, the runner also requires a diagnostic proving that the relevant calls
ran on an AP. Signal tests also require proof of AP delivery, AP handler return
and AP exit requests. Pipe tests require AP wait publication and AP syscall
resumption as well as I/O and wake diagnostics. Logs are saved in `src/build/smp-user/`. The default accelerator
is KVM; `QEMU_ACCEL=tcg` selects emulation, `OVMF_CODE` selects the firmware,
and `SMP_TEST_TIMEOUT` changes the per-guest timeout in seconds.

`sh src/tests/smp_user.sh queues` runs batches of 4 and 15 CPU-bound workers,
with a barrier after each computation round and an independent jump-ahead
reference for the results. Workers do not make syscalls during the computation
phase. Multi-CPU runs require local AP queue rotation and BSP work reclamation;
all workers must complete every barrier with the correct result.

`sh src/tests/smp_user.sh stealing` releases 28 initially gated workers with
short and long jobs, then sends signals while they execute. It checks exact
results against a jump-ahead reference, SSE contents and a distinct FS-base
value in each process. On 8 CPUs, the test requires proof that a process taken
from another AP's queue actually returned to userspace on the receiving AP.
The 1- and 2-CPU runs exercise the same workload and the no-other-AP fallback.
