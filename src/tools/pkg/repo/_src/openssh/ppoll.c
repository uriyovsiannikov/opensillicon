/* OpenSillicon currently exposes poll(2), but not the ppoll syscall. */
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <time.h>

int
ppoll(struct pollfd *fds, nfds_t count, const struct timespec *timeout,
    const sigset_t *mask)
{
    sigset_t oldmask;
    int ms = -1, result, saved;
    if (timeout != NULL) {
        if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
            errno = EINVAL;
            return -1;
        }
        if (timeout->tv_sec >= INT_MAX / 1000)
            ms = INT_MAX;
        else {
            long value = timeout->tv_sec * 1000L + (timeout->tv_nsec + 999999L) / 1000000L;
            ms = value > INT_MAX ? INT_MAX : (int)value;
        }
    }
    if (mask != NULL) {
        if (sigprocmask(SIG_SETMASK, mask, &oldmask) < 0)
            return -1;
        /* Bound the lost-wakeup window until the kernel offers atomic ppoll. */
        if (ms < 0 || ms > 100)
            ms = 100;
    }
    result = poll(fds, count, ms);
    saved = errno;
    if (mask != NULL && sigprocmask(SIG_SETMASK, &oldmask, NULL) < 0)
        return -1;
    errno = saved;
    return result;
}
