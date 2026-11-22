/*
 * Copyright (c) 2002, 2025, Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*

Copyright 1993, 1994, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the copyright holders shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from the copyright holders.

 * Copyright 1993, 1994 NCR Corporation - Dayton, Ohio, USA
 *
 * All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name NCR not be used in advertising
 * or publicity pertaining to distribution of the software without specific,
 * written prior permission.  NCR makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * NCR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NCR BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <unistd.h>

#define MIN_BACKLOG 128
#ifdef SOMAXCONN
#if SOMAXCONN > MIN_BACKLOG
#define BACKLOG SOMAXCONN
#endif
#endif
#ifndef BACKLOG
#define BACKLOG MIN_BACKLOG
#endif

/* TinyX uses local UNIX domain sockets only. */
#define UNIX_DIR "/tmp/.X11-unix"
#define UNIX_PATH UNIX_DIR "/X"

static XtransConnInfo
TRANS(SocketOpen) (void)

{
    XtransConnInfo	ciptr;

    prmsg (3,"SocketOpen()\n");

    if ((ciptr = calloc (1, sizeof(struct _XtransConnInfo))) == NULL)
    {
	prmsg (1, "SocketOpen: malloc failed\n");
	return NULL;
    }

    ciptr->fd = socket(AF_UNIX, SOCK_STREAM, 0);

#if defined(X11_t) && !defined(USE_POLL)
    if (ciptr->fd >= sysconf(_SC_OPEN_MAX))
    {
	prmsg (2, "SocketOpen: socket() returned out of range fd %d\n",
	       ciptr->fd);
	close (ciptr->fd);
	ciptr->fd = -1;
    }
#endif

    if (ciptr->fd < 0) {
	prmsg (2, "SocketOpen: socket() failed for local transport\n");

	free (ciptr);
	return NULL;
    }

    /*
     * Some systems provide a really small default buffer size for
     * UNIX sockets.  Bump it up a bit such that large transfers don't
     * proceed at glacial speed.
     */
#ifdef SO_SNDBUF
    {
	socklen_t len = sizeof (int);
	int val;

	if (getsockopt (ciptr->fd, SOL_SOCKET, SO_SNDBUF,
	    (char *) &val, &len) == 0 && val < 64 * 1024)
	{
	    val = 64 * 1024;
	    setsockopt (ciptr->fd, SOL_SOCKET, SO_SNDBUF,
	        (char *) &val, sizeof (int));
	}
    }
#endif

    return ciptr;
}




/*
 * These functions are the interface supplied in the Xtransport structure
 */



#ifdef TRANS_SERVER

static XtransConnInfo
TRANS(SocketOpenCOTSServer) (Xtransport *thistrans, const char *protocol,
			     const char *host, const char *port)

{
    XtransConnInfo ciptr;

    prmsg (2,"SocketOpenCOTSServer(%s,%s,%s)\n", protocol, host, port);

    ciptr = TRANS(SocketOpen)();
    if (!ciptr)
        return NULL;
    return ciptr;
}

#endif /* TRANS_SERVER */





#ifdef UNIXCONN
static int
set_sun_path(const char *port, const char *upath, char *path)
{
    struct sockaddr_un s;
    size_t maxlen = sizeof(s.sun_path) - 1;

    if (!port || !*port || !path)
        return -1;
    if (*port == '/')
        upath = "";
    if (strlen(upath) + strlen(port) > maxlen)
        return -1;
    snprintf(path, sizeof(s.sun_path), "%s%s", upath, port);
    return 0;
}
#endif

#ifdef TRANS_SERVER

static int
TRANS(SocketCreateListener) (XtransConnInfo ciptr,
			     struct sockaddr *sockname,
			     int socknamelen, unsigned int flags)

{
    socklen_t namelen = socknamelen;
    int	fd = ciptr->fd;

    prmsg (3, "SocketCreateListener(%p,%d)\n", (void *) ciptr, fd);

    while (bind (fd, sockname, namelen) < 0)
    {
	if (errno == EADDRINUSE) {
	    if (flags & ADDR_IN_USE_ALLOWED)
		break;
	    else
		return TRANS_ADDR_IN_USE;
	}

	prmsg (1, "SocketCreateListener: failed to bind listener\n");
	close (fd);
	return TRANS_CREATE_LISTENER_FAILED;
    }

    if (listen (fd, BACKLOG) < 0)
    {
	prmsg (1, "SocketCreateListener: listen() failed\n");
	close (fd);
	return TRANS_CREATE_LISTENER_FAILED;
    }

    /* Set a flag to indicate that this connection is a listener */

    ciptr->flags = 1 | (ciptr->flags & TRANS_KEEPFLAGS);

    return 0;
}



#ifdef UNIXCONN

static int
TRANS(SocketUNIXCreateListener) (XtransConnInfo ciptr, const char *port,
                                 unsigned int flags)
{
    struct sockaddr_un sockname;
    int namelen, status;
    char tmpport[108];


    memset(&sockname, 0, sizeof(sockname));
    sockname.sun_family = AF_UNIX;
    if (!(port && *port)) {
        snprintf(tmpport, sizeof(tmpport), "%s%ld", UNIX_PATH, (long)getpid());
        port = tmpport;
    }
    if (set_sun_path(port, UNIX_PATH, sockname.sun_path) != 0)
        return TRANS_CREATE_LISTENER_FAILED;

    mode_t old_umask = umask(0);
    int directory_result = mkdir(UNIX_DIR, 01777);
    int directory_error = errno;
    umask(old_umask);
    if (directory_result < 0 && directory_error != EEXIST) {
        errno = directory_error;
        return TRANS_CREATE_LISTENER_FAILED;
    }
    namelen = offsetof(struct sockaddr_un, sun_path) + strlen(sockname.sun_path) + 1;
    status = TRANS(SocketCreateListener)(ciptr, (struct sockaddr *)&sockname,
                                         namelen, flags);
    if (status < 0)
        return status;

    ciptr->addrlen = namelen;
    ciptr->addr = malloc(ciptr->addrlen);
    if (!ciptr->addr) {
        close(ciptr->fd);
        ciptr->fd = -1;
        unlink(sockname.sun_path);
        return TRANS_CREATE_LISTENER_FAILED;
    }
    ciptr->family = AF_UNIX;
    memcpy(ciptr->addr, &sockname, ciptr->addrlen);
    return 0;
}

static int
TRANS(SocketUNIXResetListener) (XtransConnInfo ciptr)
{
    struct sockaddr_un *unsock = (struct sockaddr_un *)ciptr->addr;
    int result;

    if (!unsock) {
        errno = EOPNOTSUPP;
        return TRANS_RESET_FAILURE;
    }
    close(ciptr->fd);
    unlink(unsock->sun_path);
    if ((ciptr->fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        return TRANS_RESET_FAILURE;

    result = bind(ciptr->fd, (struct sockaddr *)unsock, ciptr->addrlen);
    if (result < 0 || listen(ciptr->fd, BACKLOG) < 0) {
        close(ciptr->fd);
        ciptr->fd = -1;
        return TRANS_RESET_FAILURE;
    }
    return TRANS_RESET_NEW_FD;
}

#endif /* UNIXCONN */




#ifdef UNIXCONN
static XtransConnInfo
TRANS(SocketUNIXAccept) (XtransConnInfo ciptr, int *status)

{
    XtransConnInfo newciptr = calloc(1, sizeof(struct _XtransConnInfo));
    struct sockaddr_un local, peer;
    socklen_t local_length = sizeof(local), peer_length = sizeof(peer);

    if (!newciptr) {
        *status = TRANS_ACCEPT_BAD_MALLOC;
        return NULL;
    }
    newciptr->fd = accept(ciptr->fd, NULL, NULL);
    if (newciptr->fd < 0) {
        free(newciptr);
        *status = TRANS_ACCEPT_FAILED;
        return NULL;
    }
    if (getsockname(newciptr->fd, (struct sockaddr *)&local, &local_length) < 0 ||
        getpeername(newciptr->fd, (struct sockaddr *)&peer, &peer_length) < 0 ||
        local_length > sizeof(local) || peer_length > sizeof(peer)) {
        close(newciptr->fd);
        free(newciptr);
        *status = TRANS_ACCEPT_FAILED;
        return NULL;
    }
    newciptr->addr = malloc(local_length);
    newciptr->peeraddr = malloc(peer_length);
    if (!newciptr->addr || !newciptr->peeraddr) {
        close(newciptr->fd);
        free(newciptr->addr);
        free(newciptr->peeraddr);
        free(newciptr);
        *status = TRANS_ACCEPT_BAD_MALLOC;
        return NULL;
    }
    memcpy(newciptr->addr, &local, local_length);
    memcpy(newciptr->peeraddr, &peer, peer_length);
    newciptr->addrlen = local_length;
    newciptr->peeraddrlen = peer_length;
    newciptr->family = AF_UNIX;
    *status = 0;
    return newciptr;
}

#endif /* UNIXCONN */

#endif /* TRANS_SERVER */




static int
TRANS(SocketBytesReadable) (XtransConnInfo ciptr, BytesReadable_t *pend)

{
    prmsg (2,"SocketBytesReadable(%p,%d,%p)\n",
	(void *) ciptr, ciptr->fd, (void *) pend);
    return ioctl (ciptr->fd, FIONREAD, (char *) pend);
}


static int
TRANS(SocketRead) (XtransConnInfo ciptr, char *buf, int size)

{
    prmsg (2,"SocketRead(%d,%p,%d)\n", ciptr->fd, (void *) buf, size);
    return read (ciptr->fd, buf, size);
}

static int
TRANS(SocketReadv) (XtransConnInfo ciptr, struct iovec *buf, int size)

{
    prmsg (2,"SocketReadv(%d,%p,%d)\n", ciptr->fd, (void *) buf, size);
    return READV (ciptr, buf, size);
}


static int
TRANS(SocketWritev) (XtransConnInfo ciptr, struct iovec *buf, int size)

{
    prmsg (2,"SocketWritev(%d,%p,%d)\n", ciptr->fd, (void *) buf, size);

    return WRITEV (ciptr, buf, size);
}


static int
TRANS(SocketWrite) (XtransConnInfo ciptr, const char *buf, int size)

{
    prmsg (2,"SocketWrite(%d,%p,%d)\n", ciptr->fd, (const void *) buf, size);

    return write (ciptr->fd, buf, size);
}

static int
TRANS(SocketDisconnect) (XtransConnInfo ciptr)

{
    prmsg (2,"SocketDisconnect(%p,%d)\n", (void *) ciptr, ciptr->fd);

    return shutdown (ciptr->fd, 2); /* disallow further sends and receives */
}




#ifdef UNIXCONN
static int
TRANS(SocketUNIXClose) (XtransConnInfo ciptr)
{
    prmsg (2,"SocketUNIXClose(%p,%d)\n", (void *) ciptr, ciptr->fd);
    int result = close(ciptr->fd);
    if ((ciptr->flags & 1) && ciptr->addr)
        unlink(((struct sockaddr_un *)ciptr->addr)->sun_path);
    return result;
}

#endif /* UNIXCONN */



#ifdef UNIXCONN
static Xtransport	TRANS(SocketLocalFuncs) = {
	/* Socket Interface */
	"local",
	TRANS_LOCAL,

#ifdef TRANS_SERVER
	TRANS(SocketOpenCOTSServer),
#endif /* TRANS_SERVER */
#ifdef TRANS_SERVER
	TRANS(SocketUNIXCreateListener),
	TRANS(SocketUNIXResetListener),
	TRANS(SocketUNIXAccept),
#endif /* TRANS_SERVER */

	TRANS(SocketBytesReadable),
	TRANS(SocketRead),
	TRANS(SocketWrite),
	TRANS(SocketReadv),
	TRANS(SocketWritev),
	TRANS(SocketDisconnect),
	TRANS(SocketUNIXClose),
	};

#endif /* UNIXCONN */
