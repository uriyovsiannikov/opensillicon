#ifndef XMMS_PTHREAD_H
#define XMMS_PTHREAD_H
/* The cooperative runtime owns these types, including when sys/types.h is
 * included later. Keep musl from declaring a second, incompatible layout. */
#define __DEFINED_pthread_t
#define __DEFINED_pthread_attr_t
#define __DEFINED_pthread_mutex_t
#define __DEFINED_pthread_mutexattr_t
#define __DEFINED_pthread_cond_t
#include "tinycthread.h"
typedef thrd_t pthread_t;
typedef mtx_t pthread_mutex_t;
typedef cnd_t pthread_cond_t;
typedef int pthread_attr_t;
typedef int pthread_mutexattr_t;
#define PTHREAD_MUTEX_INITIALIZER {0,0,0}
#define PTHREAD_COND_INITIALIZER {0}
int pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
int pthread_join(pthread_t,void **);
void pthread_exit(void *) __attribute__((noreturn));
int pthread_mutex_init(pthread_mutex_t *,const pthread_mutexattr_t *);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutex_trylock(pthread_mutex_t *);
int pthread_cond_init(pthread_cond_t *,const void *);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_wait(pthread_cond_t *,pthread_mutex_t *);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);
int pthread_attr_init(pthread_attr_t *);
#endif
