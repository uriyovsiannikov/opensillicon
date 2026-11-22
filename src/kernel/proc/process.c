#include "kernel/hda_audio.h"
#include <stdint.h>

#include "abi/syscall.h"
#include "arch/x86_64/fpu.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/user.h"
#include "kernel/credentials.h"
#include "kernel/elf.h"
#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/network_service.h"
#include "kernel/usb_host.h"
#include "kernel/pmm.h"
#include "kernel/smp.h"
#include "kernel/spinlock.h"
#include "kernel/timer.h"
#include "kernel/process.h"
#include "kernel/tty.h"
#include "kernel/syscall.h"
#include "kernel/vmm.h"
#include "kernel/vfs.h"

#define PROCESS_KERNEL_STACK_SIZE 65536U
#define PROCESS_STACK_SIZE (64ULL * 1024ULL)
#define PROCESS_STACK_PAGE_COUNT (PROCESS_STACK_SIZE / 4096ULL)
#define PROCESS_STACK_TOP (0x00007ffffffff000ULL - 0x1000ULL)
#define PROCESS_STACK_PAGE (PROCESS_STACK_TOP - PROCESS_STACK_SIZE)
#define AUXV_AT_NULL 0ULL
#define AUXV_AT_PHDR 3ULL
#define AUXV_AT_PHENT 4ULL
#define AUXV_AT_PHNUM 5ULL
#define AUXV_AT_PAGESZ 6ULL
#define AUXV_AT_BASE 7ULL
#define AUXV_AT_ENTRY 9ULL
#define PROCESS_WAIT_ANY UINT64_MAX
#define PROCESS_USER_BASE 0x10000ULL
#define PROCESS_USER_END 0x00007ffffffff000ULL

enum process_block_reason {
	PROCESS_BLOCK_NONE,
	PROCESS_BLOCK_WAIT,
	PROCESS_BLOCK_SLEEP,
	PROCESS_BLOCK_CHANNEL
};

struct process_tls_metadata {
	bool present;
	uint64_t image_address;
	uint64_t file_offset;
	uint64_t file_size;
	uint64_t memory_size;
	uint64_t alignment;
};

#define PROCESS_SIGNAL_DEPTH_LIMIT 16U
struct process_signal_context {
 uint64_t restorer;
 uint64_t handler_stack;
 uint64_t saved_mask;
 struct interrupt_frame frame;
 _Alignas(16) uint8_t fpu_state[ARCH_FPU_STATE_SIZE];
};
_Static_assert(_Alignof(struct process_signal_context) >= 16,
 "signal FXSAVE state must remain aligned in nested contexts");

struct process {
	uint64_t id;
	char name[OS_PROCESS_NAME_SIZE];
	struct os_credentials credentials;
	uint32_t file_creation_mask;
	enum process_state state;
	bool resume_on_bsp;
	enum process_slice_stop ap_stop_reason;
	uint8_t exit_status;
	uint8_t exit_signal;
	uint8_t requested_exit_status;
	uint64_t waiting_for;
	uint64_t wake_tick;
	uint64_t select_deadline;
	uint64_t nanosleep_start_tick;
	int64_t nanosleep_seconds;
	int64_t nanosleep_nanoseconds;
	uint64_t nanosleep_remaining_address;
	bool nanosleep_active;
	bool timer_wait_on_ap;
	bool timer_resume_pending;
	void *blocked_channel;
	enum process_block_reason block_reason;
	bool local_syscall_pending;
	uint64_t local_wait_generation;
	bool retry_pending;
	bool retry_in_progress;
	struct process *parent;
	struct process *previous;
	struct process *next;
	struct address_space *address_space;
	struct elf_loaded_image *loaded_image;
	struct process_tls_metadata tls;
	void *kernel_stack;
	struct interrupt_frame *saved_frame;
	uint64_t initial_user_stack;
	uint64_t user_fs_base;
	_Alignas(16) uint8_t fpu_state[ARCH_FPU_STATE_SIZE];
	struct fd_table descriptors;
	struct os_signal_state signals;
	char current_directory[VFS_PATH_MAX];
	unsigned int signal_depth;
	struct process_signal_context signal_stack[PROCESS_SIGNAL_DEPTH_LIMIT];
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	bool test_private_mapping_child;
	uint64_t test_private_mapping_base;
	uint64_t test_private_mapping_physical;
#endif
};

_Static_assert(_Alignof(struct process) >= 16,
	"process FXSAVE state must be 16-byte aligned");

static struct process *process_head;
static struct process *ap_dispatch_cursor;
static struct process *fallback_current_process;
static uint64_t next_process_id = 1;
static uint64_t user_preemptions;
static uint64_t pipe_blocked_ticks;
static unsigned int reported_ap_stops;
static uint64_t reported_ap_local_syscalls;
static uint64_t reported_ap_private_memory;
static uint64_t reported_ap_shared_memory;
static uint64_t reported_ap_signal_calls;
static uint64_t reported_ap_signal_delivery;
static uint64_t reported_ap_signal_return;
static uint64_t reported_ap_pipe_io;
static uint64_t reported_ap_pipe_vectors;
static bool reported_reschedule_ipi;
static uint64_t reported_ap_queue_rotation;
static uint64_t reported_ap_stolen_run;
static bool reported_bsp_steal;
static size_t ap_steal_cpu_cursor;
static uint32_t remote_channel_wake;
static uint64_t channel_wake_generation;
static bool reported_ap_local_wait;
static bool reported_local_wait_race;
static uint64_t reported_ap_wait_resume;
static uint64_t reported_ap_timer_wait;
static uint64_t reported_ap_timer_resume;
static bool reported_remote_channel_wake;
static bool initialized;
/* Protect signal queues, masks and action pairs across BSP senders and APs.
 * No scheduler operation may run while this lock is held. */
static struct spinlock signal_lock = SPINLOCK_INITIALIZER;

static struct process **
process_current_slot(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	return cpu == NULL ? &fallback_current_process : &cpu->current_process;
}

/* The process table and its scheduler are still owned by the BSP. */
static bool
process_table_owned_here(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	return cpu == NULL || cpu->bsp;
}

#define current_process (*process_current_slot())
#ifdef KERNEL_TEST_SHM_LIFECYCLE
static uint64_t test_shm_reap_releases;
static uint64_t test_shm_exec_releases;
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
static bool test_private_mapping_armed;
static bool test_private_mapping_failed;
static uint64_t test_private_mapping_faults;
static uint64_t test_private_mapping_zombies;
static uint64_t test_private_mapping_reaps;
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_PROTECTION
static bool test_user_fault_capture_armed;
static bool test_user_fault_capture_valid;
static uint64_t test_user_fault_vector;
static uint64_t test_user_fault_error;
#endif

static struct process *find_child(struct process *parent, uint64_t pid,
	bool zombie_only);

static struct process_tls_metadata
process_tls_from_plan(const struct elf_load_plan *plan)
{
	return (struct process_tls_metadata){
		.present = plan->tls_present,
		.image_address = plan->tls_image_address,
		.file_offset = plan->tls_file_offset,
		.file_size = plan->tls_file_size,
		.memory_size = plan->tls_memory_size,
		.alignment = plan->tls_alignment
	};
}

static bool
signal_pending_locked(const struct process *process)
{
	unsigned int signo;

	for (signo = 1; signo < OS_SIGNAL_COUNT; signo++) {
		if ((process->signals.pending & (1ULL << signo)) != 0 &&
		    os_signal_valid(signo) &&
		    (signo == OS_SIGKILL ||
		    ((process->signals.blocked & (1ULL << signo)) == 0 &&
		    process->signals.actions[signo].handler != OS_SIGNAL_IGNORE)))
			return true;
	}
	return false;
}

static bool
process_has_pending_signal(const struct process *process)
{
	uint64_t flags = spinlock_lock_irqsave(&signal_lock);
	bool pending = signal_pending_locked(process);

	spinlock_unlock_irqrestore(&signal_lock, flags);
	return pending;
}

bool
process_current_signal_pending(void)
{
	return current_process != NULL && process_has_pending_signal(current_process);
}

static void
complete_timer_wait(struct process *process)
{
	process->wake_tick = 0;
	process->nanosleep_active = false;
	process->nanosleep_remaining_address = 0;
	process->blocked_channel = NULL;
	process->block_reason = PROCESS_BLOCK_NONE;
	process->timer_resume_pending = process->timer_wait_on_ap;
	process->timer_wait_on_ap = false;
	process->state = PROCESS_READY;
}

static void
process_set_interrupted_result(struct process *process)
{
	if (process->nanosleep_active &&
	    process->nanosleep_remaining_address != 0) {
		struct { int64_t tv_sec; int64_t tv_nsec; } remaining;
		uint64_t elapsed = timer_ticks() - process->nanosleep_start_tick;
		uint64_t elapsed_ticks = elapsed > 0 ? elapsed - 1 : 0;
		uint64_t elapsed_seconds = elapsed_ticks / 100U;
		uint64_t elapsed_nanoseconds = (elapsed_ticks % 100U) * 10000000U;
		remaining.tv_sec = process->nanosleep_seconds;
		remaining.tv_nsec = process->nanosleep_nanoseconds;
		if (remaining.tv_nsec < (int64_t)elapsed_nanoseconds) {
			if (remaining.tv_sec > 0) {
				remaining.tv_sec--;
				remaining.tv_nsec += 1000000000LL;
			} else {
				remaining.tv_sec = 0;
				remaining.tv_nsec = 0;
			}
		}
		if (remaining.tv_sec >= (int64_t)elapsed_seconds) {
			remaining.tv_sec -= (int64_t)elapsed_seconds;
			remaining.tv_nsec -= (int64_t)elapsed_nanoseconds;
		} else {
			remaining.tv_sec = 0;
			remaining.tv_nsec = 0;
		}
		if (remaining.tv_sec < 0 || remaining.tv_nsec < 0) {
			remaining.tv_sec = 0;
			remaining.tv_nsec = 0;
		}
		if (!vmm_copy_to_user(process->address_space,
		    process->nanosleep_remaining_address, &remaining,
		    sizeof(remaining)))
			process->saved_frame->rax = EFAULT;
		else
			process->saved_frame->rax = EINTR;
	} else {
		process->saved_frame->rax = EINTR;
	}
	process->saved_frame->rflags |= 1U;
	process->waiting_for = 0;
	process->wake_tick = 0;
	process->select_deadline = 0;
	process->nanosleep_active = false;
	process->timer_wait_on_ap = false;
	process->timer_resume_pending = false;
	process->nanosleep_remaining_address = 0;
	process->blocked_channel = NULL;
	process->block_reason = PROCESS_BLOCK_NONE;
	process->local_syscall_pending = false;
	process->retry_pending = false;
	process->retry_in_progress = false;
	/* AP-owned contexts remain RUNNING until their release is collected. */
	if (process_table_owned_here() && process->state != PROCESS_RUNNING)
		process->state = PROCESS_READY;
}

static void
process_wake_retry(struct process *process)
{
	process->wake_tick = 0;
	process->waiting_for = 0;
	process->block_reason = PROCESS_BLOCK_NONE;
	process->retry_pending = !process->local_syscall_pending;
	process->state = PROCESS_READY;
	if (!process->local_syscall_pending && process->saved_frame->rip >= 2)
		process->saved_frame->rip -= 2;
}

/* Called only after the BSP owns the process again. The generation covers
 * notifications consumed before an AP's block request was collected. */
static bool
publish_local_wait(struct process *process, bool from_ap)
{
	if (process_has_pending_signal(process)) {
		process_set_interrupted_result(process);
		return false;
	}
	if (__atomic_load_n(&channel_wake_generation, __ATOMIC_ACQUIRE) !=
	    process->local_wait_generation) {
		process->blocked_channel = NULL;
		process->block_reason = PROCESS_BLOCK_NONE;
		process->wake_tick = 0;
		process->state = PROCESS_READY;
		if (from_ap && !reported_local_wait_race) {
			reported_local_wait_race = true;
			klog("OpenSillicon: SMP readiness raced with AP wait publication\n");
		}
		return false;
	}
	process->state = PROCESS_BLOCKED;
	return true;
}

static void
publish_timer_wait(struct process *process)
{
	if (timer_ticks() >= process->wake_tick)
		complete_timer_wait(process);
	else if (process_has_pending_signal(process))
		process_set_interrupted_result(process);
	else
		process->state = PROCESS_BLOCKED;
}

void
process_channel_wait_begin(void)
{
	if (current_process != NULL)
		current_process->local_wait_generation =
		    __atomic_load_n(&channel_wake_generation, __ATOMIC_ACQUIRE);
}

/* APs coalesce readiness notifications without retaining channel pointers.
 * Retry all channel waiters on the BSP; each syscall rechecks its object. */
static bool
drain_remote_channel_wakes(void)
{
	if (__atomic_exchange_n(&remote_channel_wake, 0, __ATOMIC_ACQUIRE) == 0 ||
	    process_head == NULL)
		return false;
	bool woke = false;
	struct process *process = process_head;
	do {
		if (process->state == PROCESS_BLOCKED &&
		    process->block_reason == PROCESS_BLOCK_CHANNEL) {
			process->blocked_channel = NULL;
			process_wake_retry(process);
			woke = true;
		}
		process = process->next;
	} while (process != process_head);
	if (woke && !reported_remote_channel_wake) {
		reported_remote_channel_wake = true;
		klog("OpenSillicon: SMP channel waiter woken by AP\n");
	}
	return woke;
}

static void
process_wake_for_signal(struct process *process)
{
	if (process->state != PROCESS_BLOCKED ||
	    process->block_reason == PROCESS_BLOCK_NONE)
		return;
	if (process->block_reason == PROCESS_BLOCK_WAIT &&
	    find_child(process, process->waiting_for, true) != NULL) {
		process_wake_retry(process);
		return;
	}
	if (!process_has_pending_signal(process))
		return;
	if (process->block_reason == PROCESS_BLOCK_SLEEP &&
	    timer_ticks() >= process->wake_tick) {
		complete_timer_wait(process);
		return;
	}
	process_set_interrupted_result(process);
}

void
process_syscall_retry_begin(void)
{
	if (current_process == NULL)
		return;
	current_process->retry_in_progress = current_process->retry_pending;
	current_process->retry_pending = false;
}

uint64_t
process_select_deadline(void)
{
	return current_process == NULL ? 0 : current_process->select_deadline;
}

void
process_select_deadline_set(uint64_t deadline)
{
	if (current_process != NULL)
		current_process->select_deadline = deadline;
}

void
process_select_deadline_clear(void)
{
	if (current_process != NULL)
		current_process->select_deadline = 0;
}

void
process_capture_user_fs_base(uint64_t base)
{
	if (current_process != NULL) {
		/* Never restore an out-of-range value through WRMSR. User mode may
		 * have changed FSBASE directly if FSGSBASE was enabled on entry. */
		current_process->user_fs_base = base == 0 ||
		    (base >= PROCESS_USER_BASE && base < PROCESS_USER_END) ? base : 0;
	}
}

void
process_restore_user_fs_base(void)
{
	struct smp_cpu *cpu = smp_current_cpu();
	if (current_process != NULL && cpu != NULL && !cpu->bsp && cpu->user_current_stolen)
		__atomic_store_n(&cpu->user_stolen_run, true, __ATOMIC_RELAXED);
	if (current_process != NULL && current_process->timer_resume_pending) {
		struct smp_cpu *cpu = smp_current_cpu();
		if (cpu != NULL && !cpu->bsp)
			__atomic_store_n(&cpu->user_timer_resume, true, __ATOMIC_RELAXED);
		current_process->timer_resume_pending = false;
	}
	arch_fs_base_write(current_process == NULL ? 0 :
	    current_process->user_fs_base);
}

uint64_t
process_set_fs_base(uint64_t base)
{
	if (current_process == NULL)
		return EINVAL;
	if (base == 0) {
		current_process->user_fs_base = 0;
		return 0;
	}
	if (base < PROCESS_USER_BASE || base >= PROCESS_USER_END ||
	    (base & (sizeof(uint64_t) - 1)) != 0)
		return EINVAL;
	if (!vmm_validate_user_write(current_process->address_space, base,
	    sizeof(uint64_t)))
		return EFAULT;
	current_process->user_fs_base = base;
	return 0;
}

static uint64_t
process_kernel_stack_top(const struct process *process)
{
	return (uintptr_t)process->kernel_stack + PROCESS_KERNEL_STACK_SIZE;
}

static void
insert_process(struct process *process)
{
	if (process_head == NULL) {
		process_head = process;
		process->next = process;
		process->previous = process;
		return;
	}
	process->next = process_head;
	process->previous = process_head->previous;
	process_head->previous->next = process;
	process_head->previous = process;
}

static void
remove_process(struct process *process)
{
	if (ap_dispatch_cursor == process)
		ap_dispatch_cursor = process->next == process ? NULL : process->next;
	if (process->next == process) {
		process_head = NULL;
	} else {
		process->previous->next = process->next;
		process->next->previous = process->previous;
		if (process_head == process)
			process_head = process->next;
	}
	process->next = NULL;
	process->previous = NULL;
}

static void
release_loaded_image(struct elf_loaded_image *image)
{
	if (image == NULL)
		return;
	if (!address_space_destroy_with_pages(&image->address_space) ||
	    !kheap_free(image->page_addresses) || !kheap_free(image)) {
		klog("OpenSillicon panic: failed to release process image\n");
		kernel_halt();
	}
}

static void
release_process(struct process *process)
{
	fd_table_destroy(&process->descriptors);
	if (process->loaded_image != NULL) {
		release_loaded_image(process->loaded_image);
	} else if (process->address_space != NULL) {
		if (!address_space_destroy_with_pages(process->address_space) ||
		    !kheap_free(process->address_space)) {
			klog("OpenSillicon panic: failed to release process address space\n");
			kernel_halt();
		}
	}
	if (!kheap_free(process->kernel_stack) || !kheap_free(process)) {
		klog("OpenSillicon panic: failed to release process resources\n");
		kernel_halt();
	}
}

bool
process_ap_has_assignment(void)
{
	struct smp_cpu *cpu = smp_current_cpu();
	if (cpu == NULL || cpu->bsp)
		return false;
	for (size_t i = 0; i < SMP_USER_QUEUE_CAPACITY; i++)
		if (__atomic_load_n(&cpu->user_queue[i].state, __ATOMIC_ACQUIRE) ==
		    SMP_USER_SLOT_RUNNABLE)
			return true;
	return false;
}

/* A RESERVED slot protects its pointer from producers and consumers. Only
 * inactive RUNNABLE frames can migrate; the source AP has left their CR3/stack. */
static bool
steal_for_ap(struct smp_cpu *target)
{
	size_t count = smp_discovered_count();
	if (count <= 2)
		return false;
	struct smp_user_slot *destination = NULL;
	for (size_t q = 0; q < SMP_USER_QUEUE_CAPACITY; q++) {
		uint32_t expected = SMP_USER_SLOT_EMPTY;
		if (__atomic_compare_exchange_n(&target->user_queue[q].state, &expected,
		    SMP_USER_SLOT_RESERVED, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
			destination = &target->user_queue[q];
			break;
		}
	}
	if (destination == NULL)
		return false;
	for (size_t n = 0; n < count - 1; n++) {
		size_t i = 1 + ((target->user_steal_cursor + n) % (count - 1));
		struct smp_cpu *source = smp_cpu_at(i);
		if (source == target ||
		    __atomic_load_n(&source->state, __ATOMIC_ACQUIRE) != SMP_CPU_ONLINE)
			continue;
		size_t runnable = 0;
		for (size_t q = 0; q < SMP_USER_QUEUE_CAPACITY; q++) {
			uint32_t state = __atomic_load_n(&source->user_queue[q].state, __ATOMIC_ACQUIRE);
			if (state == SMP_USER_SLOT_RUNNABLE || state == SMP_USER_SLOT_ACTIVE)
				runnable++;
		}
		/* Avoid passing a sole process back and forth between idle CPUs. */
		if (runnable < 2)
			continue;
		for (size_t q = 0; q < SMP_USER_QUEUE_CAPACITY; q++) {
			struct smp_user_slot *slot = &source->user_queue[q];
			uint32_t expected = SMP_USER_SLOT_RUNNABLE;
			if (!__atomic_compare_exchange_n(&slot->state, &expected, SMP_USER_SLOT_RESERVED,
			    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
				continue;
			struct process *process = __atomic_load_n(&slot->process, __ATOMIC_RELAXED);
			destination->stolen = true;
			__atomic_store_n(&destination->process, process, __ATOMIC_RELAXED);
			/* No process dereference after publication: the destination can
			 * already be reclaimed by the BSP and eventually reaped there. */
			__atomic_store_n(&destination->state, SMP_USER_SLOT_RUNNABLE, __ATOMIC_RELEASE);
			__atomic_store_n(&slot->process, NULL, __ATOMIC_RELAXED);
			__atomic_store_n(&slot->state, SMP_USER_SLOT_EMPTY, __ATOMIC_RELEASE);
			target->user_steal_cursor = i % (count - 1);
			return true;
		}
	}
	__atomic_store_n(&destination->state, SMP_USER_SLOT_EMPTY, __ATOMIC_RELEASE);
	return false;
}

void
process_ap_run_assignment(void)
{
	struct smp_cpu *cpu = smp_current_cpu();
	if (cpu == NULL || cpu->bsp)
		return;
	if (!process_ap_has_assignment())
		(void)steal_for_ap(cpu);
	struct smp_user_slot *slot = NULL;
	for (size_t n = 0; n < SMP_USER_QUEUE_CAPACITY; n++) {
		size_t index = (cpu->user_queue_cursor + n) % SMP_USER_QUEUE_CAPACITY;
		uint32_t expected = SMP_USER_SLOT_RUNNABLE;
		if (__atomic_compare_exchange_n(&cpu->user_queue[index].state, &expected,
		    SMP_USER_SLOT_ACTIVE, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			slot = &cpu->user_queue[index];
			cpu->user_queue_cursor = (index + 1) % SMP_USER_QUEUE_CAPACITY;
			break;
		}
	}
	if (slot == NULL)
		return;
	struct process *process = __atomic_load_n(&slot->process, __ATOMIC_RELAXED);
	if (cpu->user_last_slice_local && cpu->user_last_pid != process->id)
		__atomic_store_n(&cpu->user_queue_rotation, true, __ATOMIC_RELAXED);

	__asm__ volatile ("cli" : : : "memory");
	cpu->user_current_stolen = slot->stolen;
	current_process = process;
	if (!address_space_activate(process->address_space)) {
		klog("OpenSillicon panic: AP cannot load process address space\n");
		kernel_halt();
	}
	gdt_set_kernel_stack(process_kernel_stack_top(process));
	arch_fpu_restore(process->fpu_state);
	(void)user_resume(process->saved_frame);
	arch_fpu_restore_kernel_state();
	gdt_restore_kernel_stack();
	if (!address_space_activate(vmm_kernel_address_space())) {
		klog("OpenSillicon panic: AP cannot restore kernel address space\n");
		kernel_halt();
	}
	current_process = NULL;
	cpu->user_current_stolen = false;
	bool local = process->ap_stop_reason == PROCESS_SLICE_TIMER;
	cpu->user_last_pid = process->id;
	cpu->user_last_slice_local = local;
	/* No process access after this release: another CPU may reclaim it. */
	__atomic_store_n(&slot->state,
	    local ? SMP_USER_SLOT_RUNNABLE : SMP_USER_SLOT_COMPLETE, __ATOMIC_RELEASE);
	if (!local)
		(void)smp_kick_bsp();
	__asm__ volatile ("sti" : : : "memory");
}

_Noreturn void
process_ap_stop(struct interrupt_frame *frame, enum process_slice_stop reason)
{
	struct process *process = current_process;

	if (process == NULL || process->state != PROCESS_RUNNING) {
		klog("OpenSillicon panic: AP released a context it does not own\n");
		kernel_halt();
	}
	process->saved_frame = frame;
	process->ap_stop_reason = reason;
	arch_fpu_save(process->fpu_state);
	user_return_to_kernel(0);
}

static bool
collect_ap_processes(void)
{
	bool running = false;

	for (size_t i = 1; i < smp_discovered_count(); i++) {
		struct smp_cpu *cpu = smp_cpu_at(i);
		if ((reported_ap_queue_rotation & (UINT64_C(1) << i)) == 0 &&
		    __atomic_load_n(&cpu->user_queue_rotation, __ATOMIC_RELAXED)) {
			reported_ap_queue_rotation |= UINT64_C(1) << i;
			klog("OpenSillicon: SMP AP switched local user queue on CPU ");
			klog_u64(i);
			klog("\n");
		}
		if ((reported_ap_stolen_run & (UINT64_C(1) << i)) == 0 &&
		    __atomic_load_n(&cpu->user_stolen_run, __ATOMIC_RELAXED)) {
			reported_ap_stolen_run |= UINT64_C(1) << i;
			klog("OpenSillicon: SMP stolen user process resumed on AP ");
			klog_u64(i);
			klog("\n");
		}
		for (size_t q = 0; q < SMP_USER_QUEUE_CAPACITY; q++) {
			struct smp_user_slot *slot = &cpu->user_queue[q];
			uint32_t state = __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
			if (state == SMP_USER_SLOT_EMPTY)
				continue;
			if (state != SMP_USER_SLOT_COMPLETE) {
				running = true;
				continue;
			}
			struct process *process = __atomic_load_n(&slot->process, __ATOMIC_RELAXED);
			__atomic_store_n(&slot->process, NULL, __ATOMIC_RELAXED);
			__atomic_store_n(&slot->state, SMP_USER_SLOT_EMPTY, __ATOMIC_RELEASE);
			process->resume_on_bsp = process->ap_stop_reason == PROCESS_SLICE_SYSCALL ||
			    process->ap_stop_reason == PROCESS_SLICE_FAULT ||
			    process->ap_stop_reason == PROCESS_SLICE_EXIT;
			process->state = PROCESS_READY;
			if (process->ap_stop_reason == PROCESS_SLICE_SLEEP)
				publish_timer_wait(process);
			if (process->ap_stop_reason == PROCESS_SLICE_BLOCK &&
			    publish_local_wait(process, true) && !reported_ap_local_wait) {
				reported_ap_local_wait = true;
				klog("OpenSillicon: SMP local channel wait published from AP\n");
			}

			uint64_t cpu_bit = UINT64_C(1) << i;
			if ((reported_ap_local_syscalls & cpu_bit) == 0 &&
			    __atomic_load_n(&cpu->user_local_syscalls, __ATOMIC_RELAXED) != 0) {
				reported_ap_local_syscalls |= cpu_bit;
				klog("OpenSillicon: SMP process-local syscall completed on AP ");
				klog_u64(i);
				klog("\n");
			}

			if ((reported_ap_private_memory & cpu_bit) == 0 &&
			    __atomic_load_n(&cpu->user_private_memory_calls, __ATOMIC_RELAXED)) {
				reported_ap_private_memory |= cpu_bit;
				klog("OpenSillicon: SMP private memory syscall completed on AP ");
				klog_u64(i);
				klog("\n");
			}

			if ((reported_ap_shared_memory & cpu_bit) == 0 &&
			    __atomic_load_n(&cpu->user_shared_memory_calls, __ATOMIC_RELAXED)) {
				reported_ap_shared_memory |= cpu_bit;
				klog("OpenSillicon: SMP shared memory syscall completed on AP ");
				klog_u64(i);
				klog("\n");
			}

			if ((reported_ap_signal_calls & cpu_bit) == 0 && __atomic_load_n(&cpu->user_signal_calls, __ATOMIC_RELAXED)) {
				reported_ap_signal_calls |= cpu_bit;
				klog("OpenSillicon: SMP signal syscall completed on AP ");
				klog_u64(i);
				klog("\n");
			}

			if ((reported_ap_signal_delivery & cpu_bit) == 0 && __atomic_load_n(&cpu->user_signal_delivery, __ATOMIC_RELAXED)) {
				reported_ap_signal_delivery |= cpu_bit;
				klog("OpenSillicon: SMP signal delivered on AP ");
				klog_u64(i);
				klog("\n");
			}
			if ((reported_ap_signal_return & cpu_bit) == 0 && __atomic_load_n(&cpu->user_signal_return, __ATOMIC_RELAXED)) {
				reported_ap_signal_return |= cpu_bit;
				klog("OpenSillicon: SMP signal returned on AP ");
				klog_u64(i);
				klog("\n");
			}

			if ((reported_ap_pipe_io & cpu_bit) == 0 && __atomic_load_n(&cpu->user_pipe_io, __ATOMIC_RELAXED)) {
				reported_ap_pipe_io |= cpu_bit;
				klog("OpenSillicon: SMP pipe I/O completed on AP ");
				klog_u64(i);
				klog("\n");
			}

			if ((reported_ap_pipe_vectors & cpu_bit) == 0 && __atomic_load_n(&cpu->user_pipe_vectors, __ATOMIC_RELAXED)) {
				reported_ap_pipe_vectors |= cpu_bit;
				klog("OpenSillicon: SMP pipe vector I/O completed on AP ");
				klog_u64(i);
				klog("\n");
			}

			if ((reported_ap_wait_resume & cpu_bit) == 0 && __atomic_load_n(&cpu->user_local_wait_resume, __ATOMIC_RELAXED)) {
				reported_ap_wait_resume |= cpu_bit;
				klog("OpenSillicon: SMP blocked syscall resumed on AP ");
				klog_u64(i);
				klog("\n");
			}

			if ((reported_ap_timer_wait & cpu_bit) == 0 && __atomic_load_n(&cpu->user_timer_wait, __ATOMIC_RELAXED)) {
				reported_ap_timer_wait |= cpu_bit;
				klog("OpenSillicon: SMP timer wait submitted on AP ");
				klog_u64(i);
				klog("\n");
			}
			if ((reported_ap_timer_resume & cpu_bit) == 0 && __atomic_load_n(&cpu->user_timer_resume, __ATOMIC_RELAXED)) {
				reported_ap_timer_resume |= cpu_bit;
				klog("OpenSillicon: SMP timer wait resumed on AP ");
				klog_u64(i);
				klog("\n");
			}

			unsigned int reason_bit = 1U << process->ap_stop_reason;
			if ((reported_ap_stops & reason_bit) == 0) {
				static const char *const reasons[] = {
					[PROCESS_SLICE_TIMER] = "timer",
					[PROCESS_SLICE_SYSCALL] = "syscall",
					[PROCESS_SLICE_FAULT] = "fault",
					[PROCESS_SLICE_EXIT] = "exit",
					[PROCESS_SLICE_BLOCK] = "block",
					[PROCESS_SLICE_SLEEP] = "sleep"
				};
				reported_ap_stops |= reason_bit;
				klog("OpenSillicon: SMP user slice ended on AP ");
				klog_u64(i);
				klog(" via ");
				klog(reasons[process->ap_stop_reason]);
				klog("\n");
			}
		}
	}
	return running;
}

static bool
dispatch_ap_processes(void)
{
	bool dispatched = false;
	if (process_head == NULL)
		return false;
	for (;;) {
		/* Fill the least occupied online queue, so a batch does not all
		 * land on the first AP. COMPLETE slots wait for BSP collection. */
		struct smp_cpu *target = NULL;
		size_t target_slot = 0, least = SMP_USER_QUEUE_CAPACITY;
		for (size_t i = 1; i < smp_discovered_count(); i++) {
			struct smp_cpu *cpu = smp_cpu_at(i);
			if (__atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) != SMP_CPU_ONLINE)
				continue;
			size_t occupied = 0, free_slot = 0;
			for (size_t q = 0; q < SMP_USER_QUEUE_CAPACITY; q++) {
				if (__atomic_load_n(&cpu->user_queue[q].state, __ATOMIC_ACQUIRE) != SMP_USER_SLOT_EMPTY)
					occupied++;
				else
					free_slot = q;
			}
			if (occupied < least) {
				least = occupied;
				target = cpu;
				target_slot = free_slot;
			}
		}
		if (target == NULL)
			break;
		struct process *start = ap_dispatch_cursor == NULL ? process_head : ap_dispatch_cursor;
		struct process *process = start;
		do {
			if (process != current_process && process->state == PROCESS_READY &&
			    process->saved_frame != NULL && !process->resume_on_bsp && !process->retry_pending)
				break;
			process = process->next;
		} while (process != start);
		if (process == current_process || process->state != PROCESS_READY ||
		    process->saved_frame == NULL || process->resume_on_bsp || process->retry_pending)
			break;
		uint32_t expected = SMP_USER_SLOT_EMPTY;
		if (!__atomic_compare_exchange_n(&target->user_queue[target_slot].state, &expected,
		    SMP_USER_SLOT_RESERVED, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
			continue;
		ap_dispatch_cursor = process->next;
		process->state = PROCESS_RUNNING;
		target->user_queue[target_slot].stolen = false;
		__atomic_store_n(&target->user_queue[target_slot].process, process, __ATOMIC_RELAXED);
		__atomic_store_n(&target->user_queue[target_slot].state, SMP_USER_SLOT_RUNNABLE, __ATOMIC_RELEASE);
		(void)smp_kick_cpu(target->logical_id);
		dispatched = true;
	}
	return dispatched;
}

static struct process *
steal_ap_process(bool need_work)
{
	size_t count = smp_discovered_count();
	if (count <= 1)
		return NULL;
	for (size_t n = 0; n < count - 1; n++) {
		size_t i = 1 + ((ap_steal_cpu_cursor - 1 + n) % (count - 1));
		struct smp_cpu *cpu = smp_cpu_at(i);
		if (!need_work) {
			size_t runnable = 0;
			for (size_t q = 0; q < SMP_USER_QUEUE_CAPACITY; q++) {
				uint32_t state = __atomic_load_n(&cpu->user_queue[q].state, __ATOMIC_ACQUIRE);
				if (state == SMP_USER_SLOT_RUNNABLE || state == SMP_USER_SLOT_ACTIVE)
					runnable++;
			}
			/* A busy BSP must leave a sole runnable process on its AP. */
			if (runnable < 2)
				continue;
		}
		for (size_t q = 0; q < SMP_USER_QUEUE_CAPACITY; q++) {
			struct smp_user_slot *slot = &cpu->user_queue[q];
			uint32_t expected = SMP_USER_SLOT_RUNNABLE;
			if (!__atomic_compare_exchange_n(&slot->state, &expected, SMP_USER_SLOT_RESERVED,
			    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
				continue;
			/* The AP cannot touch this frame after publishing RUNNABLE. */
			struct process *process = __atomic_load_n(&slot->process, __ATOMIC_RELAXED);
			__atomic_store_n(&slot->process, NULL, __ATOMIC_RELAXED);
			__atomic_store_n(&slot->state, SMP_USER_SLOT_EMPTY, __ATOMIC_RELEASE);
			process->state = PROCESS_READY;
			ap_steal_cpu_cursor = 1 + (i % (count - 1));
			if (!reported_bsp_steal) {
				reported_bsp_steal = true;
				klog("OpenSillicon: SMP BSP stole a queued user process\n");
			}
			return process;
		}
	}
	return NULL;
}

static struct process *
find_next_ready(struct process *after)
{
	struct process *process;

	if (process_head == NULL)
		return NULL;
	process = after == NULL ? process_head : after->next;
	do {
		if (process->state == PROCESS_READY)
			return process;
		process = process->next;
	} while (process != (after == NULL ? process_head : after->next));
	return NULL;
}

static struct interrupt_frame *
schedule_from(struct process *after)
{
	struct process *next;

	if (current_process != NULL)
		arch_fpu_save(current_process->fpu_state);
	for (;;) {
		(void)drain_remote_channel_wakes();
		bool ap_running = collect_ap_processes();
		ap_running |= dispatch_ap_processes();
		next = find_next_ready(after);
		if (next == NULL || next == current_process) {
			struct process *stolen = steal_ap_process(next == NULL);
			if (stolen != NULL)
				next = stolen;
		}
		if (next != NULL)
			break;
		if (!ap_running && current_process != NULL &&
		    current_process->parent == NULL &&
		    current_process->state == PROCESS_ZOMBIE) {
			uint64_t status = current_process->exit_status;

			current_process = NULL;
			arch_fpu_restore_kernel_state();
			if (!address_space_activate(vmm_kernel_address_space())) {
				klog("OpenSillicon panic: cannot restore kernel address space\n");
				kernel_halt();
			}
			user_return_to_kernel(status);
		}
		if (process_head != NULL) {
			struct process *process = process_head;
			bool blocked_exists = ap_running;

			do {
				if (process->state == PROCESS_BLOCKED) {
					blocked_exists = true;
					break;
				}
				process = process->next;
			} while (process != process_head);
			if (blocked_exists) {
				arch_fpu_restore_kernel_state();
				__asm__ volatile ("sti; hlt; cli" : : : "memory");
				/* The timer ISR only wakes/schedules; deferred work runs here,
				 * after it has returned to this blocked-syscall idle path. */
				(void)network_service_request(NETWORK_SERVICE_POLL_RX);
				(void)network_service_run(NETWORK_SERVICE_MAX_EVENTS, NULL);
				usb_host_poll();
				hda_audio_poll();
				continue;
			}
		}
		klog("OpenSillicon panic: no runnable process\n");
		kernel_halt();
	}
	current_process = next;
	current_process->state = PROCESS_RUNNING;
	if (current_process->saved_frame == NULL ||
	    !address_space_activate(current_process->address_space)) {
		klog("OpenSillicon panic: invalid process context\n");
		kernel_halt();
	}
	arch_fpu_restore(current_process->fpu_state);
	gdt_set_kernel_stack(process_kernel_stack_top(current_process));
	return current_process->saved_frame;
}

static void
initialize_user_frame(struct interrupt_frame *frame, uint64_t entry,
	uint64_t stack)
{
	uint64_t *words = (uint64_t *)frame;
	size_t i;

	for (i = 0; i < sizeof(*frame) / sizeof(*words); i++)
		words[i] = 0;
	frame->rip = entry;
	frame->cs = 0x23;
	frame->rflags = 0x202;
	frame->user_rsp = stack;
	frame->user_ss = 0x1b;
}

static bool
build_initial_stack(const struct elf_loaded_image *image, uint64_t physical,
	uint64_t *stack_pointer, size_t argc, const char *const argv[],
    size_t envc, const char *const envp[])
{
	volatile uint64_t *stack;
	volatile uint8_t *stack_bytes;
	uint64_t top = PROCESS_STACK_TOP;
	uint64_t words[PROCESS_EXEC_ARG_MAX + PROCESS_EXEC_ENV_MAX + 17U];
	uint64_t byte_count;
	uint64_t rsp;
	uint64_t offset;
	uint64_t string_cursor;
	uint64_t string_bytes = 0;
	uint64_t hhdm = vmm_hhdm_offset();
	uint64_t i;

	if (image == NULL || stack_pointer == NULL || argc > PROCESS_EXEC_ARG_MAX ||
	    (argc != 0 && argv == NULL) || envc > PROCESS_EXEC_ENV_MAX ||
        (envc != 0 && envp == NULL) ||
	    image->plan.program_header_address == 0 ||
	    image->plan.program_header_entry_size != 56 ||
	    image->plan.program_header_count == 0 ||
	    image->entry != image->plan.entry)
		return false;
	for (i = 0; i < argc; i++) {
		uint64_t length = 0;
		if (argv[i] == NULL)
			return false;
		while (length <= PROCESS_EXEC_ARG_BYTES_MAX && argv[i][length] != '\0')
			length++;
		if (length >= PROCESS_EXEC_ARG_BYTES_MAX ||
		    string_bytes > PROCESS_EXEC_ARG_BYTES_MAX - length - 1)
			return false;
		string_bytes += length + 1;
	}
    for(i=0;i<envc;i++) {
        size_t length=0;
        if(!envp[i])return false;
        while(length<PROCESS_EXEC_ENV_BYTES_MAX && envp[i][length])length++;
        if(length==PROCESS_EXEC_ENV_BYTES_MAX ||
            string_bytes+length+1>PROCESS_EXEC_ARG_BYTES_MAX+PROCESS_EXEC_ENV_BYTES_MAX)return false;
        string_bytes+=length+1;
    }
	byte_count = (argc + envc + 17U) * sizeof(uint64_t);
	if (byte_count > PROCESS_STACK_SIZE ||
	    string_bytes > PROCESS_STACK_SIZE - byte_count - 15)
		return false;
	/* Keep argv/env/auxv words and their strings in disjoint stack ranges. */
	rsp = (top - byte_count - string_bytes) & ~15ULL;
	if (rsp < PROCESS_STACK_PAGE || rsp > top ||
	    byte_count > top - rsp || hhdm > UINT64_MAX - physical ||
	    PROCESS_STACK_SIZE - 1 > UINT64_MAX - (hhdm + physical))
		return false;
	stack = (volatile uint64_t *)(uintptr_t)(hhdm + physical);
	stack_bytes = (volatile uint8_t *)(uintptr_t)(hhdm + physical);
	offset = (rsp - PROCESS_STACK_PAGE) / sizeof(uint64_t);
	if ((rsp - PROCESS_STACK_PAGE) % sizeof(uint64_t) != 0 ||
	    offset > PROCESS_STACK_SIZE / sizeof(uint64_t) ||
	    argc + envc + 17U >
	    PROCESS_STACK_SIZE / sizeof(uint64_t) - offset)
		return false;
	words[0] = argc;
	string_cursor = rsp + byte_count;
	for (i = 0; i < argc; i++) {
		uint64_t j = 0;
		words[1 + i] = string_cursor;
		for (;;) {
			uint8_t byte = (uint8_t)argv[i][j];
			stack_bytes[string_cursor - PROCESS_STACK_PAGE + j] = byte;
			j++;
			if (byte == 0)
				break;
		}
		string_cursor += j;
	}
	words[1 + argc] = 0;
    for(i=0;i<envc;i++) {
        size_t j=0;words[2+argc+i]=string_cursor;
        do {stack_bytes[string_cursor-PROCESS_STACK_PAGE+j]=(uint8_t)envp[i][j];}while(envp[i][j++]);
        string_cursor+=j;
    }
	words[2 + argc + envc] = 0;
	words[3 + argc + envc] = AUXV_AT_PHDR;
	words[4 + argc + envc] = image->plan.program_header_address;
	words[5 + argc + envc] = AUXV_AT_PHENT;
	words[6 + argc + envc] = image->plan.program_header_entry_size;
	words[7 + argc + envc] = AUXV_AT_PHNUM;
	words[8 + argc + envc] = image->plan.program_header_count;
	words[9 + argc + envc] = AUXV_AT_PAGESZ;
	words[10 + argc + envc] = 4096;
	words[11 + argc + envc] = AUXV_AT_ENTRY;
	words[12 + argc + envc] = image->entry;
	words[13 + argc + envc] = AUXV_AT_BASE;
	words[14 + argc + envc] = 0;
	words[15 + argc + envc] = AUXV_AT_NULL;
	words[16 + argc + envc] = 0;
	for (i = 0; i < argc + envc + 17U; i++)
		stack[offset + i] = words[i];
	*stack_pointer = rsp;
	return true;
}

static bool
map_process_stack(struct elf_loaded_image *image, bool startup_stack,
	uint64_t *stack_pointer, size_t argc, const char *const argv[],
    size_t envc, const char *const envp[])
{
	uint64_t physical;
	uint64_t initial_stack = PROCESS_STACK_TOP;
	uint64_t hhdm = vmm_hhdm_offset();
	volatile uint8_t *stack;
	uint64_t i;
	uint64_t mapped = 0;

	if (image == NULL || stack_pointer == NULL ||
	    !pmm_alloc_contiguous(PROCESS_STACK_PAGE_COUNT,
	    PROCESS_STACK_PAGE_COUNT, UINT64_MAX, &physical))
		return false;
	if (hhdm > UINT64_MAX - physical ||
	    PROCESS_STACK_SIZE - 1 > UINT64_MAX - (hhdm + physical)) {
		(void)pmm_free_contiguous(physical, PROCESS_STACK_PAGE_COUNT);
		return false;
	}
	stack = (volatile uint8_t *)(uintptr_t)(hhdm + physical);
	for (i = 0; i < PROCESS_STACK_SIZE; i++)
		stack[i] = 0;
	if (startup_stack &&
	    !build_initial_stack(image, physical, &initial_stack, argc, argv, envc, envp)) {
		(void)pmm_free_contiguous(physical, PROCESS_STACK_PAGE_COUNT);
		return false;
	}
	for (i = 0; i < PROCESS_STACK_PAGE_COUNT; i++) {
		if (!vmm_map_page(&image->address_space,
		    PROCESS_STACK_PAGE + i * 4096ULL, physical + i * 4096ULL,
		    VMM_USER | VMM_WRITE)) {
			uint64_t page;
			for (page = 0; page < mapped; page++) {
				uint64_t unmapped;
				(void)vmm_unmap_page(&image->address_space,
				    PROCESS_STACK_PAGE + page * 4096ULL, &unmapped);
			}
			(void)pmm_free_contiguous(physical,
			    PROCESS_STACK_PAGE_COUNT);
			return false;
		}
		mapped++;
	}
	*stack_pointer = initial_stack;
	return true;
}

void
process_system_init(void)
{
	if (!process_table_owned_here())
		return;
	process_head = NULL;
	ap_dispatch_cursor = NULL;
	current_process = NULL;
	next_process_id = 1;
	reported_ap_stops = 0;
	reported_ap_local_syscalls = 0;
	reported_ap_private_memory = 0;
	reported_ap_shared_memory = 0;
	reported_ap_signal_calls = 0;
	reported_ap_signal_delivery = 0;
	reported_ap_signal_return = 0;
	reported_ap_pipe_io = 0;
	reported_ap_pipe_vectors = 0;
	reported_reschedule_ipi = false;
	reported_ap_queue_rotation = 0;
	reported_ap_stolen_run = 0;
	reported_bsp_steal = false;
	ap_steal_cpu_cursor = 1;
	__atomic_store_n(&remote_channel_wake, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&channel_wake_generation, 0, __ATOMIC_RELAXED);
	reported_remote_channel_wake = false;
	reported_ap_local_wait = false;
	reported_local_wait_race = false;
	reported_ap_wait_resume = 0;
	reported_ap_timer_wait = 0;
	reported_ap_timer_resume = 0;
	__atomic_store_n(&user_preemptions, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&pipe_blocked_ticks, 0, __ATOMIC_RELAXED);
#ifdef KERNEL_TEST_SHM_LIFECYCLE
	test_shm_reap_releases = 0;
	test_shm_exec_releases = 0;
#endif
	initialized = true;
}

struct fd_table *
process_current_fd_table(void)
{
	return current_process == NULL ? NULL : &current_process->descriptors;
}

uint64_t
process_current_pid(void)
{
	return current_process == NULL ? 1 : current_process->id;
}

uint64_t
process_current_ppid(void)
{
	uint64_t flags;
	uint64_t parent_pid;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	parent_pid = current_process == NULL || current_process->parent == NULL ?
	    0 : current_process->parent->id;
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return parent_pid;
}

bool
process_current_credentials(struct os_credentials *credentials)
{
	uint64_t flags;
	if (credentials == NULL || current_process == NULL)
		return false;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	*credentials = current_process->credentials;
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return true;
}

int
process_set_uid(uint32_t id, bool effective_only)
{
	uint64_t flags;
	bool allowed;
	if (current_process == NULL)
		return -ESRCH;
	if (id == UINT32_MAX)
		return -EINVAL;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	allowed = credentials_set_uid(&current_process->credentials, id,
	    effective_only);
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return allowed ? 0 : -(int)EPERM;
}

int
process_set_gid(uint32_t id, bool effective_only)
{
	uint64_t flags;
	bool allowed;
	if (current_process == NULL)
		return -ESRCH;
	if (id == UINT32_MAX)
		return -EINVAL;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	allowed = credentials_set_gid(&current_process->credentials, id,
	    effective_only);
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return allowed ? 0 : -(int)EPERM;
}

static int
process_set_res_ids(bool group, uint32_t real, uint32_t effective,
	uint32_t saved)
{
	uint64_t flags;
	bool allowed;

	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	allowed = credentials_set_res_ids(&current_process->credentials, group,
	    real, effective, saved);
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return allowed ? 0 : -(int)EPERM;
}

int
process_set_res_uid(uint32_t real, uint32_t effective, uint32_t saved)
{
	if (current_process == NULL)
		return -(int)ESRCH;
	return process_set_res_ids(false,
	    real, effective, saved);
}

int
process_set_res_gid(uint32_t real, uint32_t effective, uint32_t saved)
{
	if (current_process == NULL)
		return -(int)ESRCH;
	return process_set_res_ids(true,
	    real, effective, saved);
}

uint32_t
process_umask(uint32_t replacement)
{
	uint64_t flags;
	uint32_t previous;
	if (current_process == NULL)
		return 0;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	previous = current_process->file_creation_mask;
	current_process->file_creation_mask = replacement & 0777U;
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return previous;
}

uint32_t
process_get_umask(void)
{
	uint64_t flags;
	uint32_t mask;
	if (current_process == NULL)
		return 0;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	mask = current_process->file_creation_mask;
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return mask;
}

int
process_chdir(const char *path)
{
	struct vnode *directory = NULL;
	struct os_credentials credentials;
	char canonical[VFS_PATH_MAX];
	int error;

	if (current_process == NULL || path == NULL)
		return -EINVAL;
	if (path[0] == '\0')
		return -ENOENT;
	credentials = current_process->credentials;
	error = vfs_resolve_path_as(current_process->current_directory, path,
	    canonical, sizeof(canonical), credentials.effective_uid,
	    credentials.effective_gid);
	if (error != 0)
		return error;
	error = vfs_lookup_as(canonical, credentials.effective_uid,
	    credentials.effective_gid, &directory);
	if (error != 0)
		return error;
	if (directory->type != VNODE_DIRECTORY) {
		vnode_put(directory);
		return -ENOTDIR;
	}
	if (!vfs_access(directory, credentials.effective_uid,
	    credentials.effective_gid, VFS_ACCESS_EXEC)) {
		vnode_put(directory);
		return -EACCES;
	}
	vnode_put(directory);
	size_t length = 0;
	while (canonical[length] != '\0')
		length++;
	if (length > 1 && canonical[length - 1] == '/')
		canonical[length - 1] = '\0';
	for (size_t i = 0; i < sizeof(current_process->current_directory); i++) {
		current_process->current_directory[i] = canonical[i];
		if (canonical[i] == '\0')
			break;
	}
	return 0;
}

bool
process_directory_busy(const char *path)
{
	if (!process_table_owned_here())
		return true;
	size_t length = 0;
	while (path[length])
		length++;
	struct process *process = process_head;
	if (!process)
		return false;
	do {
		if (process->state != PROCESS_ZOMBIE) {
			size_t i = 0;
			while (i < length &&
			    process->current_directory[i] == path[i])
				i++;
			if (i == length &&
			    (process->current_directory[i] == 0 ||
				process->current_directory[i] == '/'))
				return true;
		}
		process = process->next;
	} while (process != process_head);
	return false;
}

int
process_getcwd(char *buffer, size_t capacity)
{
	size_t length;
	if (current_process == NULL || buffer == NULL)
		return -EINVAL;
	length = 0;
	while (current_process->current_directory[length] != '\0')
		length++;
	if (length + 1U > capacity)
		return -ERANGE;
	for (size_t i = 0; i <= length; i++)
		buffer[i] = current_process->current_directory[i];
	return (int)(length + 1U);
}

int
process_resolve_path(const char *path, char *output, size_t capacity)
{
	if (current_process == NULL)
		return -EINVAL;
	return vfs_resolve_path_as(current_process->current_directory, path,
	    output, capacity, current_process->credentials.effective_uid,
	    current_process->credentials.effective_gid);
}

int
process_current_status(struct process_status *entry)
{
	struct process *process;
	uint64_t flags;
	int result = 0;

	if (entry == NULL)
		return -(int)EINVAL;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	process = current_process;
	if (process == NULL) {
		result = -(int)ESRCH;
	} else {
		*entry = (struct process_status){
			.pid = process->id,
			.parent_pid = process->parent == NULL ? 0 : process->parent->id,
			.state = (uint32_t)process->state,
			.exit_status = process->exit_status
		};
	}
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return result;
}

int
process_info_snapshot(struct os_process_info *entries, size_t capacity)
{
	struct process *process = process_head;
	size_t count = 0;

	if (!process_table_owned_here())
		return -(int)EBUSY;
	if (entries == NULL || capacity == 0 || capacity > OS_PROCESS_INFO_MAX)
		return -(int)EINVAL;
	if (process == NULL)
		return 0;
	do {
		if (count == capacity)
			return -(int)E2BIG;
		struct os_process_info *entry = &entries[count++];
		entry->pid = process->id;
		entry->parent_pid = process->parent == NULL ? 0 : process->parent->id;
		entry->state = (uint32_t)process->state;
		entry->uid = process->credentials.effective_uid;
		for (size_t i = 0; i < sizeof(entry->name); i++)
			entry->name[i] = process->name[i];
		process = process->next;
	} while (process != process_head);
	return (int)count;
}

static void
process_set_name(struct process *process, const char *path)
{
	const char *name = path;
	size_t i = 0;

	for (; *path != '\0'; path++) {
		if (*path == '/')
			name = path + 1;
	}
	for (; i + 1 < sizeof(process->name) && name[i] != '\0'; i++)
		process->name[i] = name[i];
	for (; i < sizeof(process->name); i++)
		process->name[i] = '\0';
}

static void
interrupt_ap_for_signal(const struct process *process)
{
	if (process->state != PROCESS_RUNNING || process == current_process ||
	    !process_has_pending_signal(process))
		return;
	for (size_t i = 1; i < smp_discovered_count(); i++) {
		struct smp_cpu *cpu = smp_cpu_at(i);

		for (size_t q = 0; q < SMP_USER_QUEUE_CAPACITY; q++) {
			if (__atomic_load_n(&cpu->user_queue[q].state, __ATOMIC_ACQUIRE) != SMP_USER_SLOT_EMPTY &&
			    __atomic_load_n(&cpu->user_queue[q].process, __ATOMIC_RELAXED) == process) {
				(void)smp_kick_cpu(i);
				return;
			}
		}
	}
}

uint64_t
process_kill(uint64_t pid, unsigned int signo)
{
	struct process *process;

	if (!process_table_owned_here())
		return EBUSY;
	if (pid == 0 || (int64_t)pid < 0)
		return EINVAL;
	if (signo != 0 && !os_signal_valid(signo))
		return EINVAL;
	{
		struct os_credentials sender;
		if (!process_current_credentials(&sender))
			return ESRCH;
		process = process_head;
		if (process != NULL) {
			do {
				if (process->id == pid &&
				    process->state != PROCESS_ZOMBIE) {
					const struct os_credentials *target =
					    &process->credentials;
					bool permitted = sender.effective_uid == 0 ||
					    sender.real_uid == target->real_uid ||
					    sender.real_uid == target->saved_uid ||
					    sender.effective_uid == target->real_uid ||
					    sender.effective_uid == target->saved_uid;
					if (!permitted)
						return EPERM;
					break;
				}
				process = process->next;
			} while (process != process_head);
		}
	}
	if (process_head == NULL)
		return ESRCH;
	process = process_head;
	do {
		if (process->id == pid && process->state != PROCESS_ZOMBIE) {
			if (signo == 0)
				return 0;
			uint64_t flags = spinlock_lock_irqsave(&signal_lock);
			if (signo != OS_SIGKILL &&
			    process->signals.actions[signo].handler ==
			    OS_SIGNAL_IGNORE) {
				spinlock_unlock_irqrestore(&signal_lock, flags);
				return 0;
			}
			(void)os_signal_queue(&process->signals, signo);
			spinlock_unlock_irqrestore(&signal_lock, flags);
			process_wake_for_signal(process);
			interrupt_ap_for_signal(process);
			return 0;
		}
		process = process->next;
	} while (process != process_head);
	return ESRCH;
}

int
process_signal_mask(int how, const uint64_t *set, uint64_t *old_set)
{
	if (current_process == NULL || (set != NULL && (how < 0 || how > 2)))
		return -(int)EINVAL;
	uint64_t flags = spinlock_lock_irqsave(&signal_lock);
	if (old_set != NULL)
		*old_set = current_process->signals.blocked;
	if (set != NULL) {
		switch (how) {
		case 0: current_process->signals.blocked |= *set; break;
		case 1: current_process->signals.blocked &= ~*set; break;
		case 2: current_process->signals.blocked = *set; break;
		}
		current_process->signals.blocked &= ~((1ULL << OS_SIGKILL) | (1ULL << OS_SIGSTOP));
	}
	spinlock_unlock_irqrestore(&signal_lock, flags);
	return 0;
}

static struct interrupt_frame *
process_terminate_signal(struct interrupt_frame *frame, unsigned int signo)
{
	if (current_process != NULL)
		current_process->exit_signal = signo;
	return process_exit(frame, 128U + signo);
}

static struct interrupt_frame *
process_terminate_fault(struct interrupt_frame *frame)
{
	if (current_process != NULL)
		current_process->exit_signal = 11;
	return process_exit(frame, 128U + 14U);
}

struct interrupt_frame *
process_sigaction(struct interrupt_frame *frame, unsigned int signo,
	uint64_t new_address, uint64_t old_address, uint64_t *error)
{
	static const uint8_t restorer_bytes[] = {
		0xb8, SYSCALL_SIGRETURN, 0x00, 0x00, 0x00, 0xcd, 0x80, 0x0f, 0x0b
	};
	struct os_signal_action new_action = {0};
	struct os_signal_action old_action;
	uint8_t actual_restorer[sizeof(restorer_bytes)];
	struct process *process = current_process;
	size_t i;

	if (process == NULL || !os_signal_valid(signo) ||
	    (signo == OS_SIGKILL && new_address != 0)) {
		*error = EINVAL;
		return frame;
	}
	if (old_address != 0 && !vmm_validate_user_write(process->address_space,
	    old_address, sizeof(struct os_signal_action))) {
		*error = EFAULT;
		return frame;
	}
	if (new_address != 0 && !vmm_copy_from_user(process->address_space,
	    &new_action, new_address, sizeof(struct os_signal_action))) {
		*error = EFAULT;
		return frame;
	}
	if (new_address != 0 && (new_action.flags & ~(OS_SA_RESETHAND | OS_SA_NODEFER)) != 0) {
		*error = EOPNOTSUPP;
		return frame;
	}
	new_action.mask &= ~((1ULL << OS_SIGKILL) | (1ULL << OS_SIGSTOP));
	if (new_address != 0 && !os_signal_action_valid(signo, &new_action)) {
		*error = EINVAL;
		return frame;
	}
	if (new_address != 0 && new_action.handler > OS_SIGNAL_IGNORE &&
	    (!vmm_validate_user_exec(process->address_space, new_action.handler, 1) ||
	    !vmm_validate_user_exec(process->address_space, new_action.restorer,
	    sizeof(restorer_bytes)) ||
	    !vmm_copy_from_user(process->address_space, actual_restorer,
	    new_action.restorer, sizeof(actual_restorer)))) {
		*error = EINVAL;
		return frame;
	}
	if (new_address != 0 && new_action.handler > OS_SIGNAL_IGNORE) {
		for (i = 0; i < sizeof(restorer_bytes); i++) {
			if (actual_restorer[i] != restorer_bytes[i]) {
				*error = EINVAL;
				return frame;
			}
		}
	}
	uint64_t flags = spinlock_lock_irqsave(&signal_lock);
	old_action = process->signals.actions[signo];
	if (old_address != 0 && !vmm_copy_to_user(process->address_space,
	    old_address, &old_action, sizeof(struct os_signal_action))) {
		spinlock_unlock_irqrestore(&signal_lock, flags);
		*error = EFAULT;
		return frame;
	}
	if (new_address != 0)
		(void)os_signal_set_action(&process->signals, signo, &new_action, NULL);
	spinlock_unlock_irqrestore(&signal_lock, flags);
	frame->rax = 0;
	*error = 0;
	return frame;
}

struct interrupt_frame *
process_prepare_user_return(struct interrupt_frame *frame)
{
	static const uint8_t restorer_bytes[] = {
		0xb8, SYSCALL_SIGRETURN, 0x00, 0x00, 0x00, 0xcd, 0x80, 0x0f, 0x0b
	};

	struct interrupt_frame *stack_frame = frame;

	while (frame == stack_frame && frame != NULL && (frame->cs & 3U) == 3U &&
	    current_process != NULL) {
		struct os_signal_action action;
		unsigned int signo;

		if (current_process->resume_on_bsp) {
			enum process_slice_stop reason = current_process->ap_stop_reason;
			current_process->resume_on_bsp = false;
			if (reason == PROCESS_SLICE_SYSCALL)
				frame = syscall_dispatch(frame);
			else if (reason == PROCESS_SLICE_EXIT)
				frame = process_exit(frame, current_process->requested_exit_status);
			else
				frame = process_user_exception(frame);
			continue;
		}
		if (current_process->local_syscall_pending) {
			current_process->saved_frame = frame;
			if (process_has_pending_signal(current_process)) {
				process_set_interrupted_result(current_process);
			} else {
				current_process->local_syscall_pending = false;
				frame = syscall_dispatch(frame);
				struct smp_cpu *cpu = smp_current_cpu();
				if (cpu != NULL && !cpu->bsp && (frame->rflags & 1U) == 0)
					__atomic_store_n(&cpu->user_local_wait_resume, true, __ATOMIC_RELAXED);
				continue;
			}
		}
		if (current_process->retry_pending)
			return frame;
		current_process->retry_in_progress = false;
		uint64_t flags = spinlock_lock_irqsave(&signal_lock);
		signo = os_signal_take(&current_process->signals);
		if (signo != 0)
			action = current_process->signals.actions[signo];
		spinlock_unlock_irqrestore(&signal_lock, flags);
		if (signo == 0)
			return frame;
		if (signo == OS_SIGCHLD &&
		    action.handler == OS_SIGNAL_DEFAULT)
			continue;
		if (action.handler == OS_SIGNAL_IGNORE && signo != OS_SIGKILL)
			continue;
		if (signo == OS_SIGKILL || action.handler == OS_SIGNAL_DEFAULT) {
			frame = process_terminate_signal(frame, signo);
			continue;
		}
		if (current_process->signal_depth >= PROCESS_SIGNAL_DEPTH_LIMIT) {
			frame = process_terminate_fault(frame);
			continue;
		}
		{
			uint64_t old_stack = frame->user_rsp;
			uint64_t new_stack;
			uint64_t return_address = action.restorer;
			uint8_t actual_restorer[sizeof(restorer_bytes)];
			size_t i;

			if (!os_signal_user_stack(old_stack, &new_stack)) {
				frame = process_terminate_fault(frame);
				continue;
			}
			if (!vmm_validate_user_write(current_process->address_space,
			    new_stack, (size_t)(old_stack - new_stack)) ||
			    !vmm_validate_user_exec(current_process->address_space,
			    action.handler, 1) ||
			    !vmm_validate_user_exec(current_process->address_space,
			    action.restorer, sizeof(restorer_bytes)) ||
			    !vmm_copy_from_user(current_process->address_space,
			    actual_restorer, action.restorer, sizeof(actual_restorer))) {
				frame = process_terminate_fault(frame);
				continue;
			}
			for (i = 0; i < sizeof(restorer_bytes); i++) {
				if (actual_restorer[i] != restorer_bytes[i])
					break;
			}
			if (i != sizeof(restorer_bytes) ||
			    !vmm_copy_to_user(current_process->address_space, new_stack,
			    &return_address, sizeof(return_address))) {
				frame = process_terminate_fault(frame);
				continue;
			}
			struct process_signal_context *context =
			    &current_process->signal_stack[current_process->signal_depth];
			context->frame = *frame;
			arch_fpu_save(context->fpu_state);
			uint64_t signal_flags = spinlock_lock_irqsave(&signal_lock);
			context->saved_mask = current_process->signals.blocked;
			current_process->signals.blocked |= action.mask;
			if ((action.flags & OS_SA_NODEFER) == 0)
				current_process->signals.blocked |= 1ULL << signo;
			current_process->signals.blocked &= ~((1ULL << OS_SIGKILL) | (1ULL << OS_SIGSTOP));
			if ((action.flags & OS_SA_RESETHAND) != 0)
				current_process->signals.actions[signo] = (struct os_signal_action){0};
			spinlock_unlock_irqrestore(&signal_lock, signal_flags);
			current_process->signal_depth++;
			context->restorer = action.restorer;
			context->handler_stack = new_stack;
			frame->rip = action.handler;
			frame->rdi = signo;
			frame->user_rsp = new_stack;
			struct smp_cpu *cpu = smp_current_cpu();
			if (cpu != NULL && !cpu->bsp)
				__atomic_store_n(&cpu->user_signal_delivery, true, __ATOMIC_RELAXED);
			return frame;
		}
	}
	return frame;
}

struct interrupt_frame *
process_sigreturn(struct interrupt_frame *frame, uint64_t *error)
{
	static const uint8_t restorer_bytes[] = {
		0xb8, SYSCALL_SIGRETURN, 0x00, 0x00, 0x00, 0xcd, 0x80, 0x0f, 0x0b
	};
	struct process *process = current_process;
	uint8_t actual_restorer[sizeof(restorer_bytes)];
	size_t i;

	/* Fatal validation can schedule another process on the BSP. */
	*error = 0;
	if (process == NULL || process->signal_depth == 0) {
		*error = EINVAL;
		return frame;
	}
	struct process_signal_context *context =
	    &process->signal_stack[process->signal_depth - 1];
	if (context->restorer > UINT64_MAX - 7 ||
	    frame->vector != 128U ||
	    frame->rax != SYSCALL_SIGRETURN ||
	    frame->rip != context->restorer + 7 ||
	    frame->user_rsp != context->handler_stack + 8 ||
	    !vmm_validate_user_exec(process->address_space,
	    context->restorer, sizeof(restorer_bytes)) ||
	    !vmm_copy_from_user(process->address_space, actual_restorer,
	    context->restorer, sizeof(actual_restorer)))
		return process_terminate_fault(frame);
	for (i = 0; i < sizeof(restorer_bytes); i++) {
		if (actual_restorer[i] != restorer_bytes[i])
			return process_terminate_fault(frame);
	}
	uint64_t signal_flags = spinlock_lock_irqsave(&signal_lock);
	process->signals.blocked = context->saved_mask;
	spinlock_unlock_irqrestore(&signal_lock, signal_flags);
	process->signal_depth--;
	process->block_reason = PROCESS_BLOCK_NONE;
	process->local_syscall_pending = false;
	process->retry_pending = false;
	process->retry_in_progress = false;
	*error = 0;
	/* Interrupt continuations run below the returned frame. Keep it on
	 * this process's kernel stack, never inside the heap process object. */
	*frame = context->frame;
	arch_fpu_restore(context->fpu_state);
	struct smp_cpu *cpu = smp_current_cpu();
	if (cpu != NULL && !cpu->bsp)
		__atomic_store_n(&cpu->user_signal_return, true, __ATOMIC_RELAXED);
	return frame;
}

uint64_t
process_user_preemptions(void)
{
	return __atomic_load_n(&user_preemptions, __ATOMIC_RELAXED);
}

uint64_t
process_pipe_blocked_ticks(void)
{
	return __atomic_load_n(&pipe_blocked_ticks, __ATOMIC_RELAXED);
}

#ifdef KERNEL_TEST_SHM_LIFECYCLE
uint64_t
process_test_shm_reap_releases(void)
{
	return test_shm_reap_releases;
}

uint64_t
process_test_shm_exec_releases(void)
{
	return test_shm_exec_releases;
}
#endif

static uint64_t
process_run_elf_internal(const void *image, size_t image_size,
	bool startup_stack, const char *name)
{
	struct process *root;
	uint64_t status;

	if (!process_table_owned_here() || !initialized || process_head != NULL ||
	    image == NULL)
		return UINT64_MAX;
	root = kheap_alloc(sizeof(*root));
	if (root == NULL)
		return UINT64_MAX;
	root->loaded_image = kheap_alloc(sizeof(*root->loaded_image));
	root->kernel_stack = kheap_alloc(PROCESS_KERNEL_STACK_SIZE);
	if (root->loaded_image == NULL || root->kernel_stack == NULL ||
	    !elf_image_load(image, image_size, root->loaded_image)) {
		if (root->loaded_image != NULL)
			(void)kheap_free(root->loaded_image);
		if (root->kernel_stack != NULL)
			(void)kheap_free(root->kernel_stack);
		(void)kheap_free(root);
		return UINT64_MAX;
	}
	root->id = next_process_id++;
	process_set_name(root, name);
	root->credentials = (struct os_credentials){0};
	root->file_creation_mask = 0022U;
	root->state = PROCESS_RUNNING;
	root->resume_on_bsp = false;
	root->exit_status = 0;
	root->exit_signal = 0;
	root->requested_exit_status = 0;
	root->waiting_for = 0;
	root->wake_tick = 0;
	root->select_deadline = 0;
	root->nanosleep_start_tick = 0;
	root->nanosleep_seconds = 0;
	root->nanosleep_nanoseconds = 0;
	root->nanosleep_remaining_address = 0;
	root->nanosleep_active = false;
	root->timer_wait_on_ap = false;
	root->timer_resume_pending = false;
	root->blocked_channel = NULL;
	root->block_reason = PROCESS_BLOCK_NONE;
	root->local_syscall_pending = false;
	root->local_wait_generation = 0;
	root->retry_pending = false;
	root->retry_in_progress = false;
	root->parent = NULL;
	root->current_directory[0] = '/';
	root->current_directory[1] = '\0';
	root->address_space = &root->loaded_image->address_space;
	root->tls = process_tls_from_plan(&root->loaded_image->plan);
	root->saved_frame = NULL;
	os_signal_state_init(&root->signals);
	root->signal_depth = 0;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	root->test_private_mapping_child = false;
	root->test_private_mapping_base = 0;
	root->test_private_mapping_physical = 0;
#endif
	root->initial_user_stack = PROCESS_STACK_TOP;
	root->user_fs_base = 0;
	arch_fpu_init_process_state(root->fpu_state);
	fd_table_init_standard(&root->descriptors);

	if (!map_process_stack(root->loaded_image, startup_stack,
	    &root->initial_user_stack, 0, NULL, 0, NULL)) {
		release_process(root);
		return UINT64_MAX;
	}
	insert_process(root);
	current_process = root;
	if (!address_space_activate(root->address_space)) {
		klog("OpenSillicon panic: failed to activate initial process\n");
		kernel_halt();
	}
	gdt_set_kernel_stack(process_kernel_stack_top(root));
	arch_fpu_restore(root->fpu_state);
	status = user_enter(root->loaded_image->entry, root->initial_user_stack);
	arch_fpu_restore_kernel_state();
	gdt_restore_kernel_stack();
	if (!address_space_activate(vmm_kernel_address_space())) {
		klog("OpenSillicon panic: failed to restore kernel mappings\n");
		kernel_halt();
	}
	while (process_head != NULL) {
		struct process *process = process_head;

		remove_process(process);
		release_process(process);
	}
	current_process = NULL;
	return status;
}

uint64_t
process_run_elf(const void *image, size_t image_size)
{
	return process_run_elf_internal(image, image_size, false, "elf");
}

#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
uint64_t
process_test_private_mapping_lifecycle(const void *image, size_t image_size)
{
	uint64_t status;

	test_private_mapping_armed = true;
	test_private_mapping_failed = false;
	test_private_mapping_faults = 0;
	test_private_mapping_zombies = 0;
	test_private_mapping_reaps = 0;
	status = process_run_elf_internal(image, image_size, false, "elf");
	if (status != 0 || test_private_mapping_armed ||
	    test_private_mapping_failed || test_private_mapping_faults != 1 ||
	    test_private_mapping_zombies != 1 || test_private_mapping_reaps != 1)
		return UINT64_MAX;
	return status;
}
#endif

#ifdef KERNEL_TEST_PRIVATE_MAPPING_PROTECTION
void
process_test_arm_user_fault_capture(void)
{
	test_user_fault_capture_armed = true;
	test_user_fault_capture_valid = false;
	test_user_fault_vector = UINT64_MAX;
	test_user_fault_error = UINT64_MAX;
}

bool
process_test_user_fault_capture(uint64_t *vector, uint64_t *error_code)
{
	if (!test_user_fault_capture_valid || vector == NULL || error_code == NULL)
		return false;
	*vector = test_user_fault_vector;
	*error_code = test_user_fault_error;
	test_user_fault_capture_valid = false;
	return true;
}
#endif

/* The caller retains and authorizes the inode; this kernel read does not
 * create a user-readable descriptor or resolve the pathname a second time. */
static int
read_executable_image(struct vnode *node, void **result, size_t *image_size)
{
	size_t offset = 0;
	void *image;
	if (node->size == 0 || node->size > SIZE_MAX || node->operations->read == NULL)
		return -(int)ENOEXEC;
	*image_size = (size_t)node->size;
	image = kheap_alloc(*image_size);
	if (image == NULL)
		return -(int)ENOMEM;
	while (offset < *image_size) {
		size_t amount = 0;
		int error = node->operations->read(node, offset,
		    (uint8_t *)image + offset, *image_size - offset, &amount);
		if (error != 0 || amount == 0 || amount > *image_size - offset) {
			(void)kheap_free(image);
			return error != 0 ? error : -(int)EIO;
		}
		offset += amount;
	}
	*result = image;
	return 0;
}

uint64_t
process_run_path(const char *path, bool startup_stack)
{
	struct vnode *executable = NULL;
	void *image;
	size_t image_size;
	int error;
	uint64_t result;

	if (!process_table_owned_here() || path == NULL ||
	    vfs_lookup(path, &executable) != 0)
		return UINT64_MAX;
	if (executable->type != VNODE_REGULAR ||
	    !vfs_access(executable, 0, 0, VFS_ACCESS_EXEC)) {
		vnode_put(executable);
		return UINT64_MAX;
	}
	error = read_executable_image(executable, &image, &image_size);
	vnode_put(executable);
	if (error != 0)
		return UINT64_MAX;
	/* Path-based launches always receive the standard argc/envp/auxv stack. */
	(void)startup_stack;
	result = process_run_elf_internal(image, image_size, true, path);
	(void)kheap_free(image);
	return result;
}

struct interrupt_frame *
process_timer_preempt(struct interrupt_frame *frame)
{
	struct process *next;

	if (current_process == NULL || (frame->cs & 3U) != 3U)
		return frame;
	current_process->saved_frame = frame;
	current_process->state = PROCESS_READY;
	next = find_next_ready(current_process);
	if (next != NULL && next != current_process)
		__atomic_add_fetch(&user_preemptions, 1, __ATOMIC_RELAXED);
	return schedule_from(current_process);
}

struct interrupt_frame *
process_reschedule_ipi(struct interrupt_frame *frame)
{
	/* A kernel-mode interruption only wakes the BSP. Its scheduler can
	 * collect the completion once it reaches a safe point. */
	if (!process_table_owned_here() || current_process == NULL ||
	    (frame->cs & 3U) != 3U)
		return frame;

	if (drain_remote_channel_wakes())
		return process_timer_preempt(frame);
	for (size_t i = 1; i < smp_discovered_count(); i++) {
		struct smp_cpu *cpu = smp_cpu_at(i);

		for (size_t q = 0; q < SMP_USER_QUEUE_CAPACITY; q++) {
			if (__atomic_load_n(&cpu->user_queue[q].state, __ATOMIC_ACQUIRE) != SMP_USER_SLOT_COMPLETE)
				continue;
			if (!reported_reschedule_ipi) {
				reported_reschedule_ipi = true;
				klog("OpenSillicon: SMP BSP preempted by AP completion IPI\n");
			}
			return process_timer_preempt(frame);
		}
	}
	return frame;
}

struct interrupt_frame *
process_fork(struct interrupt_frame *frame, uint64_t *error)
{
	struct process *child;
	uintptr_t stack_top;
	struct interrupt_frame *child_frame;
	uint64_t *source_words = (uint64_t *)frame;
	uint64_t *destination_words;
	size_t i;

	if (current_process == NULL) {
		*error = ECHILD;
		return frame;
	}
	child = kheap_alloc(sizeof(*child));
	if (child == NULL) {
		*error = ENOMEM;
		return frame;
	}
	arch_fpu_save(current_process->fpu_state);
	{
		size_t byte;

		for (byte = 0; byte < ARCH_FPU_STATE_SIZE; byte++)
			child->fpu_state[byte] = current_process->fpu_state[byte];
	}
	child->kernel_stack = kheap_alloc(PROCESS_KERNEL_STACK_SIZE);
	child->address_space = kheap_alloc(sizeof(*child->address_space));
	if (child->kernel_stack == NULL || child->address_space == NULL ||
	    !address_space_clone(current_process->address_space,
	    child->address_space)) {
		if (child->address_space != NULL)
			(void)kheap_free(child->address_space);
		if (child->kernel_stack != NULL)
			(void)kheap_free(child->kernel_stack);
		(void)kheap_free(child);
		*error = EAGAIN;
		return frame;
	}
	child->id = next_process_id++;
	for (i = 0; i < sizeof(child->name); i++)
		child->name[i] = current_process->name[i];
	child->credentials = current_process->credentials;
	child->file_creation_mask = current_process->file_creation_mask;
	child->state = PROCESS_READY;
	child->resume_on_bsp = false;
	child->exit_status = 0;
	child->exit_signal = 0;
	child->requested_exit_status = 0;
	child->waiting_for = 0;
	child->wake_tick = 0;
	child->select_deadline = 0;
	child->nanosleep_start_tick = 0;
	child->nanosleep_seconds = 0;
	child->nanosleep_nanoseconds = 0;
	child->nanosleep_remaining_address = 0;
	child->nanosleep_active = false;
	child->timer_wait_on_ap = false;
	child->timer_resume_pending = false;
	child->blocked_channel = NULL;
	child->block_reason = PROCESS_BLOCK_NONE;
	child->local_syscall_pending = false;
	child->local_wait_generation = 0;
	child->retry_pending = false;
	child->retry_in_progress = false;
	child->parent = current_process;
	for (i = 0; i < sizeof(child->current_directory); i++)
		child->current_directory[i] = current_process->current_directory[i];
	child->loaded_image = NULL;
	child->tls = current_process->tls;
	child->initial_user_stack = current_process->initial_user_stack;
	child->user_fs_base = current_process->user_fs_base;
	os_signal_fork_state(&current_process->signals, &child->signals);
	child->signal_depth = current_process->signal_depth;
	for (i = 0; i < child->signal_depth; i++)
		child->signal_stack[i] = current_process->signal_stack[i];
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	child->test_private_mapping_child = false;
	child->test_private_mapping_base = 0;
	child->test_private_mapping_physical = 0;
#endif
	if (!fd_table_clone(&child->descriptors, &current_process->descriptors)) {
		(void)address_space_destroy_with_pages(child->address_space);
		(void)kheap_free(child->address_space);
		(void)kheap_free(child->kernel_stack);
		(void)kheap_free(child);
		*error = EAGAIN;
		return frame;
	}
	stack_top = process_kernel_stack_top(child);
	child_frame = (struct interrupt_frame *)(stack_top - sizeof(*child_frame));
	destination_words = (uint64_t *)child_frame;
	for (i = 0; i < sizeof(*frame) / sizeof(*source_words); i++)
		destination_words[i] = source_words[i];
	child_frame->rax = 0;
	child_frame->rflags &= ~1ULL;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (test_private_mapping_armed) {
		static const uint8_t instruction = 0xc3;
		static const uint64_t marker = 0x4e585f4c49564531ULL;
		uint64_t base;

		if (vmm_private_map(child->address_space, 4096, &base) != 0 ||
		    !vmm_copy_to_user(child->address_space, base, &instruction,
		    sizeof(instruction)) ||
		    !vmm_copy_to_user(child->address_space, base + 8, &marker,
		    sizeof(marker))) {
			test_private_mapping_failed = true;
			fd_table_destroy(&child->descriptors);
			if (!address_space_destroy_with_pages(child->address_space))
				kernel_halt();
			(void)kheap_free(child->address_space);
			(void)kheap_free(child->kernel_stack);
			(void)kheap_free(child);
			*error = EAGAIN;
			return frame;
		}
		child->test_private_mapping_child = true;
		child->test_private_mapping_base = base;
		child->test_private_mapping_physical =
		    child->address_space->private_mappings->physical_pages[0];
		child_frame->rip = base;
		test_private_mapping_armed = false;
	}
#endif
	child->saved_frame = child_frame;
	insert_process(child);
	(void)dispatch_ap_processes();
	frame->rax = child->id;
	frame->rflags &= ~1ULL;
	*error = 0;
	return frame;
}

static struct interrupt_frame *
replace_process_image(struct interrupt_frame *frame, const void *image,
	size_t image_size, uint64_t invalid_elf_error, uint64_t *error,
	size_t argc, const char *const argv[], size_t envc,
    const char *const envp[], const char *name,
	const struct os_credentials *exec_credentials)
{
	struct process *process = current_process;
	struct elf_loaded_image *replacement;
	uint64_t new_entry;
	struct elf_loaded_image *old_image;
	struct address_space *old_space;
	struct interrupt_frame *new_frame;
	uintptr_t stack_top;
#ifdef KERNEL_TEST_FS_BASE
	bool had_user_fs_base;
#endif

	if (process == NULL || image == NULL || image_size == 0) {
		*error = EINVAL;
		return frame;
	}
	replacement = kheap_alloc(sizeof(*replacement));
	if (replacement == NULL || !elf_image_load(image, image_size,
	    replacement)) {
		if (replacement != NULL)
			(void)kheap_free(replacement);
		*error = invalid_elf_error;
		return frame;
	}
	if (!map_process_stack(replacement, true, &stack_top, argc, argv, envc, envp)) {
		release_loaded_image(replacement);
		*error = ENOMEM;
		return frame;
	}
	new_entry = replacement->entry;
	old_image = process->loaded_image;
	old_space = process->address_space;
#ifdef KERNEL_TEST_FS_BASE
	had_user_fs_base = process->user_fs_base != 0;
#endif
	if (!address_space_activate(&replacement->address_space)) {
		release_loaded_image(replacement);
		*error = ENOMEM;
		return frame;
	}
	fd_table_close_on_exec(&process->descriptors);
	/* Commit effective and saved IDs after image activation succeeds.
	 * A failed exec must preserve all caller credentials. */
	if (exec_credentials != NULL)
		process->credentials = *exec_credentials;
	process->credentials.saved_uid = process->credentials.effective_uid;
	process->credentials.saved_gid = process->credentials.effective_gid;
	process_set_name(process, name);
	process->loaded_image = replacement;
	process->address_space = &replacement->address_space;
	process->tls = process_tls_from_plan(&replacement->plan);
	process->user_fs_base = 0;
	arch_fpu_init_process_state(process->fpu_state);
	arch_fpu_restore(process->fpu_state);
#ifdef KERNEL_TEST_FS_BASE
	if (had_user_fs_base) {
		if (process->user_fs_base != 0 || arch_fs_base_read() != 0)
			kernel_halt();
		klog("OpenSillicon test: successful exec reset FS base\n");
	}
#endif
	if (old_image != NULL) {
#ifdef KERNEL_TEST_SHM_LIFECYCLE
		uint64_t live_before = shm_test_live_pages();
#endif
		release_loaded_image(old_image);
#ifdef KERNEL_TEST_SHM_LIFECYCLE
		if (shm_test_live_pages() < live_before)
			test_shm_exec_releases++;
#endif
	} else {
#ifdef KERNEL_TEST_SHM_LIFECYCLE
		uint64_t live_before = shm_test_live_pages();
#endif
		if (!address_space_destroy_with_pages(old_space) ||
		    !kheap_free(old_space)) {
			klog("OpenSillicon panic: exec could not release old address space\n");
			kernel_halt();
		}
#ifdef KERNEL_TEST_SHM_LIFECYCLE
		if (shm_test_live_pages() < live_before)
			test_shm_exec_releases++;
#endif
	}
	process->initial_user_stack = stack_top;
	os_signal_exec_state(&process->signals);
	process->signal_depth = 0;
	process->timer_wait_on_ap = false;
	process->timer_resume_pending = false;
	process->block_reason = PROCESS_BLOCK_NONE;
	process->local_syscall_pending = false;
	process->retry_pending = false;
	process->retry_in_progress = false;
	new_frame = (struct interrupt_frame *)(process_kernel_stack_top(process) -
	    sizeof(*new_frame));
	initialize_user_frame(new_frame, new_entry, process->initial_user_stack);
	process->saved_frame = new_frame;
	*error = 0;
	return new_frame;
}

struct interrupt_frame *
process_exec_path_env(struct interrupt_frame *frame, const char *path,
	size_t argc, const char *const argv[], size_t envc, const char *const envp[], uint64_t *error)
{
	size_t image_size;
	struct vnode *executable = NULL;
	struct os_credentials credentials;
	char canonical[VFS_PATH_MAX];
	void *image;
	struct interrupt_frame *result;
	int rc;

	if (current_process == NULL || path == NULL) {
		*error = EFAULT;
		return frame;
	}
	rc = process_resolve_path(path, canonical, sizeof(canonical));
	if (rc != 0) {
		*error = (uint64_t)-rc;
		return frame;
	}
	credentials = current_process->credentials;
	rc = vfs_lookup_as(canonical, credentials.effective_uid,
	    credentials.effective_gid, &executable);
	if (rc != 0) {
		*error = (uint64_t)-rc;
		return frame;
	}
	if (executable->type != VNODE_REGULAR || !vfs_access(executable,
	    credentials.effective_uid, credentials.effective_gid,
	    VFS_ACCESS_EXEC)) {
		vnode_put(executable);
		*error = EACCES;
		return frame;
	}
	rc = read_executable_image(executable, &image, &image_size);
	if (rc == 0) {
		if ((executable->mode & 04000U) != 0)
			credentials.effective_uid = executable->uid;
		if ((executable->mode & 02000U) != 0)
			credentials.effective_gid = executable->gid;
	}
	vnode_put(executable);
	if (rc != 0) {
		*error = (uint64_t)-rc;
		return frame;
	}
	result = replace_process_image(frame, image, image_size,
	    ENOEXEC, error, argc, argv, envc, envp, canonical, &credentials);
	(void)kheap_free(image);
	return result;
}

static struct process *
find_child(struct process *parent, uint64_t pid, bool zombie_only)
{
	struct process *process;

	if (process_head == NULL)
		return NULL;
	process = process_head;
	do {
		if (process->parent == parent &&
		    (pid == PROCESS_WAIT_ANY || process->id == pid) &&
		    (!zombie_only || process->state == PROCESS_ZOMBIE))
			return process;
		process = process->next;
	} while (process != process_head);
	return NULL;
}

static bool
reap_child(struct process *parent, struct process *child,
	uint64_t status_address, uint64_t *error)
{
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	bool test_mapping_child = child->test_private_mapping_child;
	uint64_t test_mapping_physical = child->test_private_mapping_physical;
#endif

	int32_t status = child->exit_signal ? child->exit_signal :
	    (int32_t)child->exit_status << 8;
	if (status_address != 0 && !vmm_copy_to_user(parent->address_space,
	    status_address, &status, sizeof(status))) {
		*error = EFAULT;
		return false;
	}
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (test_mapping_child &&
	    (child->state != PROCESS_ZOMBIE ||
	    child->address_space->private_mappings == NULL ||
	    child->address_space->private_mappings->base !=
	    child->test_private_mapping_base ||
	    !pmm_test_page_is_allocated(test_mapping_physical)))
		test_private_mapping_failed = true;
#endif
	remove_process(child);
#ifdef KERNEL_TEST_SHM_LIFECYCLE
	{
		uint64_t live_before = shm_test_live_pages();

		release_process(child);
		if (shm_test_live_pages() < live_before)
			test_shm_reap_releases++;
	}
#else
	release_process(child);
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (test_mapping_child) {
		if (pmm_test_page_is_allocated(test_mapping_physical))
			test_private_mapping_failed = true;
		test_private_mapping_reaps++;
	}
#endif
	*error = 0;
	return true;
}

struct interrupt_frame *
process_waitpid(struct interrupt_frame *frame, uint64_t *error)
{
	struct process *parent = current_process;
	uint64_t pid = frame->rdi;
	uint64_t options = frame->rdx;
	struct process *child;

	if ((options & ~((uint64_t)OS_WAIT_NOHANG)) != 0) {
		*error = EINVAL;
		return frame;
	}
	if (parent == NULL || (pid != PROCESS_WAIT_ANY &&
	    (int64_t)pid <= 0)) {
		*error = ECHILD;
		return frame;
	}
	child = find_child(parent, pid, true);
	if (child == NULL)
		child = find_child(parent, pid, false);
	if (child == NULL) {
		*error = ECHILD;
		return frame;
	}
	if (child->state == PROCESS_ZOMBIE) {
		uint64_t child_id = child->id;

		if (!reap_child(parent, child, frame->rsi, error))
			return frame;
		frame->rax = child_id;
		frame->rflags &= ~1ULL;
		return frame;
	}
	if ((options & OS_WAIT_NOHANG) != 0) {
		frame->rax = 0;
		frame->rflags &= ~1ULL;
		*error = 0;
		return frame;
	}
	if (process_has_pending_signal(parent)) {
		frame->rax = EINTR;
		frame->rflags |= 1U;
		parent->retry_in_progress = false;
		*error = 0;
		return frame;
	}
	parent->state = PROCESS_BLOCKED;
	parent->saved_frame = frame;
	parent->waiting_for = pid;
	parent->block_reason = PROCESS_BLOCK_WAIT;
	parent->retry_pending = false;
	parent->retry_in_progress = false;
	*error = 0;
	return schedule_from(parent);
}

static struct interrupt_frame *
park_timer_wait(struct interrupt_frame *frame, uint64_t deadline, uint64_t *error)
{
	struct process *process = current_process;
	process->wake_tick = deadline;
	process->blocked_channel = NULL;
	process->saved_frame = frame;
	process->block_reason = PROCESS_BLOCK_SLEEP;
	process->local_syscall_pending = false;
	process->retry_pending = false;
	process->retry_in_progress = false;
	process->timer_wait_on_ap = !process_table_owned_here();
	process->timer_resume_pending = false;
	frame->rax = 0;
	frame->rflags &= ~1ULL;
	*error = 0;
	if (process_has_pending_signal(process)) {
		process_set_interrupted_result(process);
		return frame;
	}
	if (!process_table_owned_here()) {
		__atomic_store_n(&smp_current_cpu()->user_timer_wait, true, __ATOMIC_RELAXED);
		process_ap_stop(frame, PROCESS_SLICE_SLEEP);
	}
	publish_timer_wait(process);
	return schedule_from(process);
}

struct interrupt_frame *
process_nanosleep(struct interrupt_frame *frame, uint64_t ticks,
	int64_t seconds, int64_t nanoseconds, uint64_t remaining_address,
	uint64_t *error)
{
	if (current_process == NULL) {
		*error = ECHILD;
		return frame;
	}
	if (ticks == 0) {
		frame->rax = 0;
		frame->rflags &= ~1ULL;
		*error = 0;
		return frame;
	}
	uint64_t now = timer_ticks();
	if (ticks > UINT64_MAX - now) {
		*error = EINVAL;
		return frame;
	}
	current_process->nanosleep_start_tick = now;
	current_process->nanosleep_seconds = seconds;
	current_process->nanosleep_nanoseconds = nanoseconds;
	current_process->nanosleep_remaining_address = remaining_address;
	current_process->nanosleep_active = true;
	return park_timer_wait(frame, now + ticks, error);
}

struct interrupt_frame *
process_block_local_on_channel(struct interrupt_frame *frame, void *channel)
{
	struct process *process = current_process;
	if (process == NULL || channel == NULL) {
		frame->rax = EINVAL;
		frame->rflags |= 1U;
		return frame;
	}
	if (process_has_pending_signal(process)) {
		frame->rax = EINTR;
		frame->rflags |= 1U;
		return frame;
	}
	process->local_syscall_pending = true;
	process->blocked_channel = channel;
	process->block_reason = PROCESS_BLOCK_CHANNEL;
	process->wake_tick = 0;
	process->retry_pending = false;
	process->retry_in_progress = false;
	process->saved_frame = frame;
	if (!process_table_owned_here())
		process_ap_stop(frame, PROCESS_SLICE_BLOCK);
	(void)publish_local_wait(process, false);
	return schedule_from(process);
}

struct interrupt_frame *
process_block_on_channel(struct interrupt_frame *frame, void *channel)
{
	return process_block_on_channel_until(frame, channel, 0);
}

struct interrupt_frame *
process_block_on_channel_until(struct interrupt_frame *frame, void *channel,
	uint64_t wake_tick)
{
	if (current_process == NULL || channel == NULL) {
		frame->rax = EINVAL;
		frame->rflags |= 1U;
		return frame;
	}
	/* AP callers reach this only after an I/O attempt made no progress.
	 * The BSP retries readiness before publishing a blocked process. */
	if (!process_table_owned_here())
		process_ap_stop(frame, PROCESS_SLICE_SYSCALL);
	if (process_has_pending_signal(current_process)) {
		frame->rax = EINTR;
		frame->rflags |= 1U;
		current_process->retry_in_progress = false;
		current_process->select_deadline = 0;
		return frame;
	}
	current_process->blocked_channel = channel;
	current_process->wake_tick = wake_tick;
	current_process->state = PROCESS_BLOCKED;
	current_process->saved_frame = frame;
	current_process->block_reason = PROCESS_BLOCK_CHANNEL;
	current_process->retry_pending = false;
	current_process->retry_in_progress = false;
	return schedule_from(current_process);
}

void
process_wake_channel(void *channel)
{
	struct process *process;

	if (channel == NULL)
		return;
	__atomic_add_fetch(&channel_wake_generation, 1, __ATOMIC_RELEASE);
	if (!process_table_owned_here()) {
		if (__atomic_exchange_n(&remote_channel_wake, 1, __ATOMIC_RELEASE) == 0)
			(void)smp_kick_bsp();
		return;
	}
	if (process_head == NULL)
		return;
	process = process_head;
	do {
		if (process->state == PROCESS_BLOCKED &&
		    process->blocked_channel == channel) {
			process->blocked_channel = NULL;
			process_wake_retry(process);
		}
		process = process->next;
	} while (process != process_head);
}

void
process_timer_wakeup(uint64_t ticks)
{
	struct process *process;

	if (!process_table_owned_here() || process_head == NULL)
		return;
	process = process_head;
	do {
		if (process->state == PROCESS_BLOCKED &&
		    process->blocked_channel != NULL &&
		    __atomic_load_n(&pipe_blocked_ticks, __ATOMIC_RELAXED) !=
		    UINT64_MAX)
			__atomic_add_fetch(&pipe_blocked_ticks, 1, __ATOMIC_RELAXED);
		if (process->state == PROCESS_BLOCKED && process->wake_tick != 0 &&
		    ticks >= process->wake_tick) {
			process->wake_tick = 0;
			if (process->block_reason == PROCESS_BLOCK_CHANNEL) {
				process->blocked_channel = NULL;
				process_wake_retry(process);
				process = process->next;
				continue;
			}
			complete_timer_wait(process);
		}
		process = process->next;
	} while (process != process_head);
}

struct interrupt_frame *
process_exit(struct interrupt_frame *frame, uint64_t status)
{
	struct process *process = current_process;
	struct process *parent;
	struct process *child;

	(void)frame;
	if (process == NULL)
		user_return_to_kernel(status);
	if (!process_table_owned_here()) {
		/* Teardown and parent notification remain owned by the BSP. */
		process->requested_exit_status = status & 0xffU;
		process_ap_stop(frame, PROCESS_SLICE_EXIT);
	}
	tty_console_framebuffer_owner_exit(process->id);
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (process->test_private_mapping_child) {
		uint64_t marker = 0;
		uint64_t changed_marker = 0x4e585f4146544552ULL;

		if (status != 128U + 14U || frame == NULL || frame->vector != 14 ||
		    frame->rip != process->test_private_mapping_base ||
		    frame->error_code != 0x15U ||
		    !vmm_validate_user_write(process->address_space,
		    process->test_private_mapping_base, 1) ||
		    vmm_validate_user_exec(process->address_space,
		    process->test_private_mapping_base, 1) ||
		    !vmm_copy_from_user(process->address_space, &marker,
		    process->test_private_mapping_base + 8, sizeof(marker)) ||
		    marker != 0x4e585f4c49564531ULL ||
		    !vmm_copy_to_user(process->address_space,
		    process->test_private_mapping_base + 16, &changed_marker,
		    sizeof(changed_marker)) ||
		    !vmm_copy_from_user(process->address_space, &marker,
		    process->test_private_mapping_base + 16, sizeof(marker)) ||
		    marker != changed_marker ||
		    !pmm_test_page_is_allocated(
		    process->test_private_mapping_physical))
			test_private_mapping_failed = true;
		else
			test_private_mapping_faults++;
	}
#endif
	process->exit_status = status & 0xffU;
	process->state = PROCESS_ZOMBIE;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE
	if (process->test_private_mapping_child) {
		if (process->address_space->private_mappings == NULL ||
		    process->address_space->private_mappings->base !=
		    process->test_private_mapping_base ||
		    !pmm_test_page_is_allocated(
		    process->test_private_mapping_physical))
			test_private_mapping_failed = true;
		else
			test_private_mapping_zombies++;
	}
#endif
	fd_table_destroy(&process->descriptors);
	parent = process->parent;
	if (process_head != NULL) {
		child = process_head;
		do {
			if (child->parent == process)
				child->parent = parent;
			child = child->next;
		} while (child != process_head);
	}
	if (parent != NULL && parent->state == PROCESS_BLOCKED &&
	    (parent->waiting_for == PROCESS_WAIT_ANY ||
	    parent->waiting_for == process->id)) {
		process_wake_retry(parent);
	}
	return schedule_from(process);
}

struct interrupt_frame *
process_user_exception(struct interrupt_frame *frame)
{
	if (!process_table_owned_here() && current_process != NULL)
		process_ap_stop(frame, PROCESS_SLICE_FAULT);
	if (current_process == NULL) {
#ifdef KERNEL_TEST_PRIVATE_MAPPING_PROTECTION
		if (test_user_fault_capture_armed) {
			test_user_fault_vector = frame->vector;
			test_user_fault_error = frame->error_code;
			test_user_fault_capture_valid = true;
			test_user_fault_capture_armed = false;
		}
#endif
		user_return_to_kernel(0x100U + frame->vector);
	}
	if (current_process != NULL)
		current_process->exit_signal = frame->vector == 0 ? 8 :
		    frame->vector == 6 ? 4 : 11;
	return process_exit(frame, 128U + frame->vector);
}
