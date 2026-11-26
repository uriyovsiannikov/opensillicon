#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "kernel/fd.h"
#include "kernel/signal_state.h"
#include "abi/credentials.h"
#include "abi/process_info.h"
#include "abi/exec.h"

enum process_state {
	PROCESS_READY = OS_PROCESS_READY,
	PROCESS_RUNNING = OS_PROCESS_RUNNING,
	PROCESS_BLOCKED = OS_PROCESS_BLOCKED,
	PROCESS_ZOMBIE = OS_PROCESS_ZOMBIE
};

void process_system_init(void);
uint64_t process_run_path(const char *path);
struct fd_table *process_current_fd_table(void);
uint64_t process_current_pid(void);
uint64_t process_current_ppid(void);
bool process_current_credentials(struct os_credentials *credentials);
int process_set_uid(uint32_t id);
int process_set_gid(uint32_t id);
int process_set_res_uid(uint32_t real, uint32_t effective, uint32_t saved);
int process_set_res_gid(uint32_t real, uint32_t effective, uint32_t saved);
uint32_t process_umask(uint32_t replacement);
uint32_t process_get_umask(void);
int process_chdir(const char *path);
bool process_directory_busy(const char *canonical_path);
int process_getcwd(char *buffer, size_t capacity);
int process_resolve_path(const char *path, char *output, size_t capacity);
int process_info_snapshot(struct os_process_info *entries, size_t capacity);
void process_syscall_retry_begin(void);
/* Snapshot readiness before a local I/O attempt; publish a wait only if
 * no event intervened. Local waits retain a kernel syscall continuation. */
void process_channel_wait_begin(void);
struct interrupt_frame *process_block_local_on_channel(
	struct interrupt_frame *frame, void *channel);
uint64_t process_select_deadline(void);
void process_select_deadline_set(uint64_t deadline);
void process_select_deadline_clear(void);
void process_capture_user_fs_base(uint64_t base);
void process_restore_user_fs_base(void);
uint64_t process_set_fs_base(uint64_t base);
uint64_t process_kill(uint64_t pid, unsigned int signo);
int process_signal_mask(int how, const uint64_t *set, uint64_t *old_set);
struct interrupt_frame *process_sigaction(struct interrupt_frame *frame,
	unsigned int signo, uint64_t new_action, uint64_t old_action,
	uint64_t *error);
enum process_slice_stop {
	PROCESS_SLICE_TIMER,
	PROCESS_SLICE_SYSCALL,
	PROCESS_SLICE_FAULT,
	PROCESS_SLICE_EXIT,
	PROCESS_SLICE_BLOCK,
	PROCESS_SLICE_SLEEP
};
bool process_ap_has_assignment(void);
void process_ap_run_assignment(void);
_Noreturn void process_ap_stop(struct interrupt_frame *frame,
	enum process_slice_stop reason);
struct interrupt_frame *process_timer_preempt(
	struct interrupt_frame *frame);
struct interrupt_frame *process_reschedule_ipi(struct interrupt_frame *frame);
struct interrupt_frame *process_fork(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_exec_path_env(struct interrupt_frame *frame,
    const char *path, size_t argc, const char *const argv[],
    size_t envc, const char *const envp[], uint64_t *error);
struct interrupt_frame *process_sigreturn(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_prepare_user_return(
	struct interrupt_frame *frame);
struct interrupt_frame *process_waitpid(struct interrupt_frame *frame,
	uint64_t *error);
struct interrupt_frame *process_nanosleep(struct interrupt_frame *frame,
	uint64_t ticks, int64_t seconds, int64_t nanoseconds,
	uint64_t remaining_address, uint64_t *error);
struct interrupt_frame *process_block_on_channel(
	struct interrupt_frame *frame, void *channel);
struct interrupt_frame *process_block_on_channel_until(
	struct interrupt_frame *frame, void *channel, uint64_t wake_tick);
void process_wake_channel(void *channel);
void process_timer_wakeup(uint64_t ticks);
int process_real_timer(uint64_t value, uint64_t interval, bool update,
    uint64_t *old_value, uint64_t *old_interval);
struct interrupt_frame *process_exit(struct interrupt_frame *frame,
	uint64_t status);
struct interrupt_frame *process_user_exception(
	struct interrupt_frame *frame);

#endif
