#include <stdio.h>
#include "../../tools/pkg/repo/_src/twm/tinyx/os/fdset.h"

int
main(void)
{
    fd_set left, right, output;
    FD_ZERO(&left); FD_ZERO(&right);
    if (XFD_ANYSET(&left)) return 1;
    for (int i = 0; i < FD_SETSIZE; i++) {
        if (i % 3 == 0) FD_SET(i, &left);
        if (i % 5 == 0) FD_SET(i, &right);
    }
    XFD_ANDSET(&output, &left, &right);
    for (int i = 0; i < FD_SETSIZE; i++)
        if (!!FD_ISSET(i, &output) != (i % 15 == 0)) return 2;
    XFD_COPYSET(&left, &output);
    XFD_ANDSET(&output, &output, &right);
    for (int i = 0; i < FD_SETSIZE; i++)
        if (!!FD_ISSET(i, &output) != (i % 15 == 0)) return 3;
    XFD_COPYSET(&right, &output);
    XFD_ORSET(&output, &left, &output);
    for (int i = 0; i < FD_SETSIZE; i++)
        if (!!FD_ISSET(i, &output) != (i % 3 == 0 || i % 5 == 0)) return 4;
    XFD_UNSET(&output, &left);
    for (int i = 0; i < FD_SETSIZE; i++)
        if (!!FD_ISSET(i, &output) != (i % 5 == 0 && i % 3 != 0)) return 5;
    XFD_UNSET(&output, &output);
    if (XFD_ANYSET(&output)) return 6;
    FD_SET(FD_SETSIZE-1, &output);
    if (!XFD_ANYSET(&output)) return 7;
    XFD_COPYSET(&output, &output);
    if (!FD_ISSET(FD_SETSIZE-1, &output)) return 8;
    puts("TINYX_FDSET_PASS");
    return 0;
}
