#ifndef TINYX_FDSET_H
#define TINYX_FDSET_H

#include <sys/select.h>

#if FD_SETSIZE < 512
#define XFD_SETSIZE FD_SETSIZE
#else
#define XFD_SETSIZE 512
#endif
#define Select select

static inline int
XFD_ANYSET(const fd_set *set)
{
    for (int fd = 0; fd < FD_SETSIZE; fd++)
        if (FD_ISSET(fd, set))
            return 1;
    return 0;
}

static inline void
XFD_COPYSET(const fd_set *source, fd_set *destination)
{
    *destination = *source;
}

static inline void
XFD_ANDSET(fd_set *destination, const fd_set *left, const fd_set *right)
{
    for (int fd = 0; fd < FD_SETSIZE; fd++) {
        if (FD_ISSET(fd, left) && FD_ISSET(fd, right))
            FD_SET(fd, destination);
        else
            FD_CLR(fd, destination);
    }
}

static inline void
XFD_ORSET(fd_set *destination, const fd_set *left, const fd_set *right)
{
    for (int fd = 0; fd < FD_SETSIZE; fd++) {
        if (FD_ISSET(fd, left) || FD_ISSET(fd, right))
            FD_SET(fd, destination);
        else
            FD_CLR(fd, destination);
    }
}

static inline void
XFD_UNSET(fd_set *destination, const fd_set *source)
{
    for (int fd = 0; fd < FD_SETSIZE; fd++)
        if (FD_ISSET(fd, source))
            FD_CLR(fd, destination);
}

#endif
