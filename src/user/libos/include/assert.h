#ifndef LIBOS_ASSERT_H
#define LIBOS_ASSERT_H
#include <stdlib.h>
#include <stdio.h>
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
static inline __attribute__((__noreturn__)) void
__libos_assert_fail(const char *expression, const char *file, int line)
{
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, expression);
    abort();
    __builtin_unreachable();
}
#define assert(expression) ((expression) ? (void)0 : \
    __libos_assert_fail(#expression, __FILE__, __LINE__))
#endif
#endif
