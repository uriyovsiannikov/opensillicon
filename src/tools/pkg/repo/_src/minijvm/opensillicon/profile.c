/* SPDX-License-Identifier: MIT */
#include "profile.h"
#include "jvm_util.h"
#include "jit.h"
#include <cpuid.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* One native thread runs all fibers. Account the running fiber only: nested
 * native calls and GC yields must not count the same wall time twice. */
int jvm_profile_enabled, jvm_profile_methods;
static FILE *output;
static struct jvm_profile_scope *current;
static int use_tsc, tsc_fallback;
static uint64_t previous_tsc;
static uint64_t epoch_ns, epoch_ticks, last, totals[PROFILE_BUCKETS];
static uint64_t calls[PROFILE_BUCKETS], frames, image_bytes, dropped_methods;
enum { METHOD_SLOTS = 16384 };
static struct method_record {
    const void *key;
    uint64_t ticks, calls;
    int caller;
    char label[144];
} methods[METHOD_SLOTS];

uint64_t jvm_profile_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static uint64_t ticks(void) {
    if (use_tsc) {
        unsigned lo, hi;
        __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
        uint64_t value=((uint64_t)hi << 32) | lo;
        if(value>=previous_tsc) {previous_tsc=value;return value;}
        /* Invariant does not promise synchronized counters on every CPU.
         * Discard the partial interval rather than double-count backward jumps. */
        use_tsc=0;tsc_fallback=1;
        epoch_ns=epoch_ticks=last=jvm_profile_now();
        memset(totals,0,sizeof totals);memset(calls,0,sizeof calls);
        for(unsigned i=0;i<METHOD_SLOTS;i++)methods[i].ticks=methods[i].calls=0;
        return last;
    }
    return jvm_profile_now();
}
void jvm_profile_init(void) {
    output = stderr;
    const char *v = getenv("OPEN_SILLICON_JVM_PROFILE");
    jvm_profile_enabled = 0;
    if (!v || strcmp(v, "1")) return;
    const char *detail=getenv("OPEN_SILLICON_JVM_PROFILE_METHODS");
    jvm_profile_methods=detail && !strcmp(detail,"1");
    unsigned a,b,c,d;
    if (__get_cpuid_max(0x80000000, 0) >= 0x80000007) {
        __cpuid(0x80000007,a,b,c,d);
        use_tsc = !!(d & (1U << 8)); /* invariant TSC; no guessed CPU frequency */
    }
    epoch_ns = jvm_profile_now();
    epoch_ticks = last = ticks();
    const char *path=getenv("OPEN_SILLICON_JVM_PROFILE_LOG");
    if (!path || !*path) path="/tmp/minijvm-profile.log";
    if (strcmp(path,"stderr")) {
        int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_NONBLOCK,0600);
        struct stat st;
        if(fd>=0) {
            if(!fstat(fd,&st) && S_ISREG(st.st_mode)) output=fdopen(fd,"w");
            if(!output || output==stderr){close(fd);output=stderr;}
        }
        if(output!=stderr) fprintf(stderr,"[MC profiler] log: %s\n",path);
    }
    /* fopen/fprintf allocate too. Start accounting only after the epoch and
     * output setup exist, so startup logging cannot count TSC uptime. */
    epoch_ns=jvm_profile_now();epoch_ticks=last=ticks();
    jvm_profile_enabled=1;
    fprintf(stderr,"[MC profiler] %s\n",jvm_profile_methods ? "full method tracing (high overhead)" : "lightweight regions; Java time includes VM bridge");
    fprintf(output,"[MC profiler] exclusive fiber wall-time, clock=%s\n",
        use_tsc ? "invariant-tsc" : "monotonic (higher overhead)");
}
static void account(uint64_t now) {
    if (now < last) { last = now; return; }
    uint64_t elapsed = now-last;
    totals[current ? current->bucket : PROFILE_OTHER] += elapsed;
    if (current && current->method >= 0) methods[current->method].ticks += elapsed;
    last = now;
}
static int method_slot(MethodInfo *m) {
    uintptr_t hash=(uintptr_t)m;
    hash ^= hash >> 33; hash *= UINT64_C(0xff51afd7ed558ccd); hash ^= hash >> 33;
    unsigned pos = hash % METHOD_SLOTS;
    for (unsigned n=0;n<64;n++,pos=(pos+1)%METHOD_SLOTS) {
        struct method_record *r=methods+pos;
        if (r->key==m) return pos;
        if (!r->key) {
            r->key=m;
            snprintf(r->label,sizeof r->label,"%s.%s%s",utf8_cstr(m->_this_class->name),
                utf8_cstr(m->name),utf8_cstr(m->descriptor));
            return pos;
        }
    }
    ++dropped_methods;
    return -1;
}
void jvm_profile_enter(struct jvm_profile_scope *s,unsigned bucket,MethodInfo *m) {
    account(ticks());
    s->parent=current;s->bucket=bucket;s->active=1;
    s->method=m ? method_slot(m) : current ? current->method : -1;
    if (m && s->method>=0) {
        ++methods[s->method].calls;
        methods[s->method].caller=current ? current->method : -1;
    }
    current=s;++calls[bucket];
}
void jvm_profile_method_enter(struct jvm_profile_scope *s,MethodInfo *m) {
    if(jvm_profile_methods || m->is_native) {
        jvm_profile_enter(s,m->is_native ? PROFILE_NATIVE : PROFILE_VM,m);
        return;
    }
    /* Nested Java calls need no timestamp/hash-table work. A native/GC/file
     * transition still opens its own scope, preserving exclusive wall time. */
    if(current && (current->bucket==PROFILE_JIT || current->bucket==PROFILE_INTERP))return;
    unsigned bucket=minijvm_jit_enabled && m->converted_code &&
        m->converted_code->jit.state==JIT_GEN_SUCCESS ? PROFILE_JIT : PROFILE_INTERP;
    jvm_profile_enter(s,bucket,0);
    s->method=-1;
}
void jvm_profile_end(struct jvm_profile_scope *s) {
    if (!s->active) return;
    account(ticks());current=s->parent;s->active=0;
}
struct jvm_profile_scope *jvm_profile_switch(struct jvm_profile_scope *next) {
    if (!jvm_profile_enabled) return 0;
    account(ticks());struct jvm_profile_scope *old=current;current=next;return old;
}
void jvm_profile_x11_bytes(uint64_t bytes) { if(jvm_profile_enabled) image_bytes+=bytes; }
/* Scale invariant-TSC ticks using this interval's observed monotonic duration.
 * This avoids a syscall per bytecode/native call and works across CPU speeds. */
static uint64_t ms(uint64_t value,uint64_t span,uint64_t elapsed) {
    return span ? (uint64_t)(((__uint128_t)value*elapsed)/span/1000000) : 0;
}
static void report(uint64_t now_ns) {
    uint64_t now=ticks();account(now);now_ns=jvm_profile_now();
    uint64_t span=now-epoch_ticks, elapsed=now_ns-epoch_ns;
    uint64_t other=0;
    for(unsigned i=PROFILE_INTERP;i<PROFILE_BUCKETS;i++) other+=totals[i];
#define MS(bucket) (unsigned long long)ms(totals[bucket],span,elapsed)
    fprintf(output,"[MC profile] frames=%llu interval_ms=%llu frame_ms=%llu GL_ms=%llu finish_ms=%llu X11_ms=%llu GC_ms=%llu other_ms=%llu GL_calls=%llu GC_count=%llu overlap=no\n",
        (unsigned long long)frames,(unsigned long long)(elapsed/1000000),
        (unsigned long long)(frames ? elapsed/frames/1000000 : 0),MS(PROFILE_GL),MS(PROFILE_FINISH),
        (unsigned long long)ms(totals[PROFILE_X11]+totals[PROFILE_X11_SCAN]+totals[PROFILE_X11_SEND]+totals[PROFILE_X11_COPY],span,elapsed),
        MS(PROFILE_GC),(unsigned long long)ms(other-totals[PROFILE_X11_SCAN]-totals[PROFILE_X11_SEND]-totals[PROFILE_X11_COPY],span,elapsed),
        (unsigned long long)calls[PROFILE_GL],(unsigned long long)calls[PROFILE_GC]);
    fprintf(output,"[MC detail] java_ms=%llu",
        (unsigned long long)ms(totals[PROFILE_INTERP]+totals[PROFILE_VM]+totals[PROFILE_JIT],span,elapsed));
    if(jvm_profile_methods)fprintf(output," interp_ms=%llu vm_bridge_ms=%llu jit_ms=%llu",
        MS(PROFILE_INTERP),MS(PROFILE_VM),MS(PROFILE_JIT));
    fprintf(output," native_ms=%llu file_ms=%llu input_ms=%llu sleep_ms=%llu scheduler_ms=%llu compile_ms=%llu alloc_ms=%llu alloc_calls=%llu misc_ms=%llu x11_scan_ms=%llu x11_send_ms=%llu x11_copy_ms=%llu x11_bytes=%llu java_regions=%llu switches=%llu\n",
        MS(PROFILE_NATIVE),MS(PROFILE_FILE),MS(PROFILE_INPUT),MS(PROFILE_WAIT),MS(PROFILE_SCHED),MS(PROFILE_COMPILE),MS(PROFILE_ALLOC),(unsigned long long)calls[PROFILE_ALLOC],MS(PROFILE_OTHER),
        MS(PROFILE_X11_SCAN),MS(PROFILE_X11_SEND),MS(PROFILE_X11_COPY),(unsigned long long)image_bytes,
        (unsigned long long)(calls[PROFILE_JIT]+calls[PROFILE_INTERP]),(unsigned long long)calls[PROFILE_SCHED]);
    if(tsc_fallback) {fprintf(output,"[MC profiler] backward TSC detected; using monotonic clock\n");tsc_fallback=0;}
    unsigned selected[5];
    for(unsigned rank=0;rank<5;rank++) {
        unsigned best=METHOD_SLOTS;
        for(unsigned i=0;i<METHOD_SLOTS;i++) {
            int used=0;for(unsigned j=0;j<rank;j++) if(selected[j]==i)used=1;
            if(!used && methods[i].ticks && (best==METHOD_SLOTS || methods[i].ticks>methods[best].ticks))best=i;
        }
        selected[rank]=best;
        if(best<METHOD_SLOTS) fprintf(output,"[MC method] self_ms=%llu calls=%llu %s\n",
            (unsigned long long)ms(methods[best].ticks,span,elapsed),(unsigned long long)methods[best].calls,methods[best].label);
    }
    /* Bounded context sample for the hottest self-time methods. */
    for(unsigned rank=0;rank<3;rank++) if(selected[rank]<METHOD_SLOTS) {
        int index=methods[selected[rank]].caller;
        fprintf(output,"[MC callers] %s",methods[selected[rank]].label);
        for(unsigned depth=0;depth<6 && index>=0 && index<METHOD_SLOTS;depth++) {
            fprintf(output," <- %s",methods[index].label);
            int next=methods[index].caller;if(next==index)break;index=next;
        }
        fputc('\n',output);
    }
    if(dropped_methods)fprintf(output,"[MC profiler] method_table_full=%llu\n",(unsigned long long)dropped_methods);
    for(unsigned i=0;i<METHOD_SLOTS;i++)methods[i].ticks=methods[i].calls=0;
    memset(totals,0,sizeof totals);memset(calls,0,sizeof calls);frames=image_bytes=dropped_methods=0;
    fflush(output);
    epoch_ns=jvm_profile_now();epoch_ticks=last=ticks();
#undef MS
}
void jvm_profile_frame(void) {
    if(!jvm_profile_enabled)return;
    ++frames;uint64_t now=jvm_profile_now();
    if(now-epoch_ns>=UINT64_C(5000000000))report(now);
}
void jvm_profile_flush(void) {
    if(jvm_profile_enabled)report(jvm_profile_now());
}
