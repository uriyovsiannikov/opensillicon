#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
struct start {
  void *(*function)(void *);
  void *argument;
};
static int entry(void *p) {
  struct start s = *(struct start *)p;
  free(p);
  s.function(s.argument);
  return 0;
}
int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *),
                   void *p) {
  (void)a;
  struct start *s = malloc(sizeof(*s));
  if (!s)
    return ENOMEM;
  s->function = f;
  s->argument = p;
  if (thrd_create(t, entry, s) != thrd_success) {
    free(s);
    return EAGAIN;
  }
  return 0;
}
int pthread_join(pthread_t t, void **value) {
  int r;
  if (thrd_join(t, &r) != thrd_success)
    return ESRCH;
  if (value)
    *value = NULL;
  return 0;
}
void pthread_exit(void *value) {
  (void)value;
  thrd_exit(0);
}
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
  (void)a;
  return mtx_init(m, mtx_plain) == thrd_success ? 0 : ENOMEM;
}
int pthread_mutex_destroy(pthread_mutex_t *m) {
  mtx_destroy(m);
  return 0;
}
int pthread_mutex_lock(pthread_mutex_t *m) {
  return mtx_lock(m) == thrd_success ? 0 : EINVAL;
}
int pthread_mutex_unlock(pthread_mutex_t *m) {
  return mtx_unlock(m) == thrd_success ? 0 : EPERM;
}
int pthread_mutex_trylock(pthread_mutex_t *m) {
  return mtx_trylock(m) == thrd_success ? 0 : EBUSY;
}
int pthread_cond_init(pthread_cond_t *c, const void *a) {
  (void)a;
  return cnd_init(c) == thrd_success ? 0 : ENOMEM;
}
int pthread_cond_destroy(pthread_cond_t *c) {
  cnd_destroy(c);
  return 0;
}
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
  return cnd_wait(c, m) == thrd_success ? 0 : EINVAL;
}
int pthread_cond_signal(pthread_cond_t *c) {
  return cnd_signal(c) == thrd_success ? 0 : EINVAL;
}
int pthread_cond_broadcast(pthread_cond_t *c) {
  return cnd_broadcast(c) == thrd_success ? 0 : EINVAL;
}
int pthread_attr_init(pthread_attr_t *a) {
  *a = 0;
  return 0;
}
int __wrap_usleep(unsigned int us) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  t.tv_sec += us / 1000000;
  t.tv_nsec += (us % 1000000) * 1000L;
  if (t.tv_nsec >= 1000000000) {
    t.tv_nsec -= 1000000000;
    t.tv_sec++;
  }
  return thrd_sleep(&t, NULL) == thrd_success ? 0 : -1;
}
int __real_poll(struct pollfd *, nfds_t, int);
int __wrap_poll(struct pollfd *p, nfds_t n, int timeout) {
  struct timespec start, now;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (;;) {
    int r = __real_poll(p, n, 0);
    if (r || timeout == 0)
      return r;
    __wrap_usleep(1000);
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (timeout > 0 && (now.tv_sec - start.tv_sec) * 1000 +
                               (now.tv_nsec - start.tv_nsec) / 1000000 >=
                           timeout)
      return 0;
  }
}
#include <sys/select.h>
int __real_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int __wrap_select(int n, fd_set *r, fd_set *w, fd_set *e,
                  struct timeval *timeout) {
  fd_set rr, ww, ee;
  struct timespec start, now;
  long long limit = -1;
  clock_gettime(CLOCK_MONOTONIC, &start);
  if (timeout)
    limit = (long long)timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
  if (r)
    rr = *r;
  if (w)
    ww = *w;
  if (e)
    ee = *e;
  for (;;) {
    struct timeval zero = {0, 0};
    if (r)
      *r = rr;
    if (w)
      *w = ww;
    if (e)
      *e = ee;
    int result = __real_select(n, r, w, e, &zero);
    if (result || limit == 0)
      return result;
    __wrap_usleep(1000);
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (limit >= 0 && (now.tv_sec - start.tv_sec) * 1000 +
                              (now.tv_nsec - start.tv_nsec) / 1000000 >=
                          limit) {
      if (r)
        FD_ZERO(r);
      if (w)
        FD_ZERO(w);
      if (e)
        FD_ZERO(e);
      return 0;
    }
  }
}
