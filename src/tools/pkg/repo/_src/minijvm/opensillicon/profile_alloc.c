/* SPDX-License-Identifier: MIT */
#include "profile.h"
#include <stddef.h>
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void*,size_t);
void __real_free(void*);
void *__wrap_malloc(size_t n) { PROFILE_SCOPE(PROFILE_ALLOC); return __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t size) { PROFILE_SCOPE(PROFILE_ALLOC); return __real_calloc(n,size); }
void *__wrap_realloc(void*p,size_t n) { PROFILE_SCOPE(PROFILE_ALLOC); return __real_realloc(p,n); }
void __wrap_free(void*p) { PROFILE_SCOPE(PROFILE_ALLOC); __real_free(p); }
