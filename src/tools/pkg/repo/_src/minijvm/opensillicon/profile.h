/* SPDX-License-Identifier: MIT */
#ifndef OPEN_SILLICON_JVM_PROFILE_H
#define OPEN_SILLICON_JVM_PROFILE_H
#include <stdint.h>
enum jvm_profile_bucket {
    PROFILE_GL, PROFILE_FINISH, PROFILE_X11, PROFILE_GC,
    PROFILE_INTERP, PROFILE_VM, PROFILE_JIT, PROFILE_NATIVE, PROFILE_FILE,
    PROFILE_INPUT, PROFILE_WAIT, PROFILE_SCHED, PROFILE_COMPILE,
    PROFILE_X11_SCAN, PROFILE_X11_SEND, PROFILE_X11_COPY,
    PROFILE_ALLOC, PROFILE_OTHER, PROFILE_BUCKETS
};
struct _MethodInfo;
struct jvm_profile_scope {
    struct jvm_profile_scope *parent;
    unsigned bucket;
    int method, active;
};
extern int jvm_profile_enabled, jvm_profile_methods;
void jvm_profile_method_enter(struct jvm_profile_scope *, struct _MethodInfo *);
void jvm_profile_init(void);
uint64_t jvm_profile_now(void);
void jvm_profile_enter(struct jvm_profile_scope *, unsigned, struct _MethodInfo *);
void jvm_profile_end(struct jvm_profile_scope *);
struct jvm_profile_scope *jvm_profile_switch(struct jvm_profile_scope *);
void jvm_profile_frame(void);
void jvm_profile_flush(void);
void jvm_profile_x11_bytes(uint64_t);
#define PROFILE_NAMED_SCOPE(name, bucket, method) \
    struct jvm_profile_scope name __attribute__((cleanup(jvm_profile_end))) = {0}; \
    if (jvm_profile_enabled) jvm_profile_enter(&name, bucket, method)
#define PROFILE_METHOD_SCOPE(name, method) \
    struct jvm_profile_scope name __attribute__((cleanup(jvm_profile_end))) = {0}; \
    if (jvm_profile_enabled) jvm_profile_method_enter(&name, method)
#define PROFILE_DETAIL_SCOPE(bucket) \
    struct jvm_profile_scope detail_scope __attribute__((cleanup(jvm_profile_end))) = {0}; \
    if (jvm_profile_enabled && jvm_profile_methods) jvm_profile_enter(&detail_scope, bucket, 0)
#define PROFILE_SCOPE(bucket) PROFILE_NAMED_SCOPE(profile_scope, bucket, 0)
#endif
