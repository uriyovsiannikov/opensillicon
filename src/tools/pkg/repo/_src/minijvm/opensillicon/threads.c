/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <cpuid.h>
#include "threads.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "tinycthread.h"
#include "profile.h"

enum { STACK_SIZE = 2 * 1024 * 1024, TLS_KEYS = 16, PAGE = 4096 };
#define MAP_FAILED ((void *)-1)
enum state { READY, WAITING, FINISHED };
struct fiber {
	struct jvm_profile_scope *profile;
	struct fiber *next;
	thrd_t id;
	uint64_t *context;
	void *mapping;
	thrd_start_t start;
	void *arg;
	enum state state;
	const void *wait_on;
	uint64_t deadline;
	int result;
	int saved_errno;
	int detached;
	void *tls[TLS_KEYS];
};
static struct fiber initial = { .id = 1 };
static struct fiber *fibers = &initial, *current = &initial;
static thrd_t last_id = 1;
static unsigned char tls_used[TLS_KEYS];
static tss_dtor_t tls_dtors[TLS_KEYS];
extern void fiber_switch(uint64_t **save, uint64_t *restore);

static uint64_t clock_ns(clockid_t clock)
{
	struct timespec ts;
	if (clock_gettime(clock, &ts))
		abort();
	return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/* tinycthread in miniJVM uses absolute wall-clock deadlines. */
static uint64_t convert_deadline(const struct timespec *absolute)
{
	if (!absolute)
		return UINT64_MAX;
	uint64_t target = (uint64_t)absolute->tv_sec * 1000000000 + absolute->tv_nsec;
	uint64_t now = clock_ns(CLOCK_REALTIME);
	return clock_ns(CLOCK_MONOTONIC) + (target > now ? target - now : 0);
}

static struct fiber *lookup(thrd_t id)
{
	for (struct fiber *f = fibers; f; f = f->next)
		if (f->id == id)
			return f;
	return NULL;
}

static void reap(void)
{
	struct fiber **link = &fibers;
	while (*link) {
		struct fiber *f = *link;
		if (f != current && f->state == FINISHED && f->detached) {
			*link = f->next;
			munmap(f->mapping, STACK_SIZE + PAGE);
			free(f);
		} else {
			link = &f->next;
		}
	}
}

static void wake(const void *event, int all)
{
	for (struct fiber *f = fibers; f; f = f->next) {
		if (f->state == WAITING && f->wait_on == event) {
			f->state = READY;
			f->wait_on = NULL;
			if (!all)
				return;
		}
	}
}

static void schedule(void)
{
	PROFILE_SCOPE(PROFILE_SCHED);
	reap();
	for (;;) {
		uint64_t now = 0, next_deadline = UINT64_MAX;
		int clock_read = 0;
		struct fiber *start = current->next ? current->next : fibers;
		struct fiber *next = start;
		do {
			if (next->state == WAITING && next->deadline != UINT64_MAX) {
				if (!clock_read) { now = clock_ns(CLOCK_MONOTONIC); clock_read = 1; }
				if (next->deadline <= now) {
					next->wait_on = NULL;
					next->result = thrd_timeout;
					next->state = READY;
				} else if (next->deadline < next_deadline) {
					next_deadline = next->deadline;
				}
			}
			if (next->state == READY) {
				if (next != current) {
					struct fiber *old = current;
					old->saved_errno = errno;
					current = next;
					errno = next->saved_errno;
					old->profile = jvm_profile_switch(next->profile);
					fiber_switch(&old->context, next->context);
				}
				return;
			}
			next = next->next ? next->next : fibers;
		} while (next != start);

		if (next_deadline == UINT64_MAX) {
			fputs("miniJVM: all threads are waiting without a deadline\n", stderr);
			abort();
		}
		uint64_t delay = next_deadline - now;
		struct timespec sleep = {delay / 1000000000, delay % 1000000000};
		{ PROFILE_SCOPE(PROFILE_WAIT); nanosleep(&sleep, NULL); }
	}
}

static int wait_for(const void *event, uint64_t deadline)
{
	current->state = WAITING;
	current->wait_on = event;
	current->deadline = deadline;
	current->result = thrd_success;
	schedule();
	return current->result;
}

static _Noreturn void run_fiber(void)
{
	thrd_exit(current->start(current->arg));
}

int thrd_create(thrd_t *id, thrd_start_t start, void *arg)
{
	struct fiber *f = calloc(1, sizeof(*f));
	if (!f)
		return thrd_nomem;
	void *stack = mmap(NULL, STACK_SIZE + PAGE, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stack == MAP_FAILED || mprotect(stack, PAGE, PROT_NONE)) {
		if (stack != MAP_FAILED)
			munmap(stack, STACK_SIZE + PAGE);
		free(f);
		return thrd_nomem;
	}
	uint64_t *context = (uint64_t *)((char *)stack + STACK_SIZE + PAGE) - 9;
	context[0] = UINT64_C(0x037f00001f80);
	context[7] = (uintptr_t)run_fiber;
	f->context = context;
	f->mapping = stack;
	f->id = ++last_id;
	f->start = start;
	f->arg = arg;
	f->next = current->next;
	current->next = f;
	*id = f->id;
	return thrd_success;
}

thrd_t thrd_current(void) { return current->id; }
int thrd_equal(thrd_t a, thrd_t b) { return a == b; }
void thrd_yield(void) { if (fibers != current || current->next) schedule(); }

/* Poll GC/suspension at the existing backedges, but switch fibers at a time
 * quantum instead of every 256 loop branches. Calibrate passively against the
 * OS clock; no assumed CPU MHz and no startup sleep. Deadlines and blocking
 * waits still go through schedule() immediately. */
static uint64_t yield_start_ticks, yield_start_ns, yield_quantum, yield_last;
static int yield_clock_checked, yield_invariant;
static uint64_t yield_ticks(void) {
    unsigned lo,hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo),"=d"(hi) :: "memory");
    return ((uint64_t)hi<<32)|lo;
}
void minijvm_cooperative_yield(void) {
    if(!yield_clock_checked) {
        unsigned a,b,c,d;
        if(__get_cpuid_max(0x80000000,0)>=0x80000007) {
            __cpuid(0x80000007,a,b,c,d);yield_invariant=!!(d&(1U<<8));
        }
        yield_clock_checked=1;
        if(yield_invariant) {yield_start_ns=clock_ns(CLOCK_MONOTONIC);yield_start_ticks=yield_ticks();}
    }
    if(yield_invariant) {
        uint64_t now=yield_ticks();
        if(yield_quantum) {
            if(now>=yield_last && now-yield_last<yield_quantum)return;
        } else {
            uint64_t ns=clock_ns(CLOCK_MONOTONIC);
            if(ns>yield_start_ns && ns-yield_start_ns>=50000000 && now>yield_start_ticks) {
                yield_quantum=(uint64_t)(((__uint128_t)(now-yield_start_ticks)*1000000)/(ns-yield_start_ns));
                if(!yield_quantum)yield_invariant=0;
            }
        }
    }
    thrd_yield();
    if(yield_invariant)yield_last=yield_ticks();
}

_Noreturn void thrd_exit(int result)
{
	if (current == &initial)
		exit(result);
	for (unsigned i = 0; i < TLS_KEYS; i++)
		if (tls_dtors[i] && current->tls[i])
			tls_dtors[i](current->tls[i]);
	current->state = FINISHED;
	current->result = result;
	wake(current, 1);
	schedule();
	abort();
}

int thrd_detach(thrd_t id)
{
	struct fiber *f = lookup(id);
	if (!f || f == &initial || f->detached)
		return thrd_error;
	f->detached = 1;
	return thrd_success;
}

int thrd_join(thrd_t id, int *result)
{
	struct fiber *f = lookup(id);
	if (!f || f == current || f == &initial || f->detached)
		return thrd_error;
	while (f->state != FINISHED)
		wait_for(f, UINT64_MAX);
	if (result)
		*result = f->result;
	f->detached = 1;
	reap();
	return thrd_success;
}

int thrd_sleep(const struct timespec *absolute, struct timespec *remaining)
{
	wait_for(NULL, convert_deadline(absolute));
	if (remaining)
		*remaining = (struct timespec){0};
	return 0;
}

int mtx_init(mtx_t *m, int type) { *m = (mtx_t){.recursive = type & mtx_recursive}; return thrd_success; }
void mtx_destroy(mtx_t *m) { (void)m; }
int mtx_trylock(mtx_t *m)
{
	if (m->owner && (m->owner != current->id || !m->recursive))
		return thrd_busy;
	m->owner = current->id;
	++m->count;
	return thrd_success;
}
int mtx_timedlock(mtx_t *m, const struct timespec *absolute)
{
	uint64_t deadline = convert_deadline(absolute);
	while (mtx_trylock(m) != thrd_success)
		if (wait_for(m, deadline) == thrd_timeout)
			return thrd_timeout;
	return thrd_success;
}
int mtx_lock(mtx_t *m) { return mtx_timedlock(m, NULL); }
int mtx_unlock(mtx_t *m)
{
	if (m->owner != current->id)
		return thrd_error;
	if (--m->count == 0) {
		m->owner = 0;
		wake(m, 0);
	}
	return thrd_success;
}

int cnd_init(cnd_t *cond) { *cond = (cnd_t){0}; return thrd_success; }
void cnd_destroy(cnd_t *cond) { (void)cond; }
int cnd_signal(cnd_t *cond) { wake(cond, 0); return thrd_success; }
int cnd_broadcast(cnd_t *cond) { wake(cond, 1); return thrd_success; }
int cnd_timedwait(cnd_t *cond, mtx_t *m, const struct timespec *absolute)
{
	if (m->owner != current->id)
		return thrd_error;
	unsigned depth = m->count;
	m->count = 1;
	mtx_unlock(m);
	int result = wait_for(cond, convert_deadline(absolute));
	mtx_lock(m);
	m->count = depth;
	return result;
}
int cnd_wait(cnd_t *cond, mtx_t *m) { return cnd_timedwait(cond, m, NULL); }

int tss_create(tss_t *key, tss_dtor_t dtor)
{
	for (unsigned i = 0; i < TLS_KEYS; i++)
		if (!tls_used[i]) {
			tls_used[i] = 1;
			tls_dtors[i] = dtor;
			*key = i;
			return thrd_success;
		}
	return thrd_error;
}
void tss_delete(tss_t key)
{
	if (key >= TLS_KEYS)
		return;
	for (struct fiber *f = fibers; f; f = f->next)
		f->tls[key] = NULL;
	tls_used[key] = 0;
	tls_dtors[key] = NULL;
}
void *tss_get(tss_t key) { return key < TLS_KEYS && tls_used[key] ? current->tls[key] : NULL; }
int tss_set(tss_t key, void *value)
{
	if (key >= TLS_KEYS || !tls_used[key])
		return thrd_error;
	current->tls[key] = value;
	return thrd_success;
}
