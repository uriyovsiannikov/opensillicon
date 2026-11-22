#ifndef KERNEL_PTY_H
#define KERNEL_PTY_H

#include "kernel/fd.h"
#include "abi/ioctl.h"
#include "abi/termios.h"

int pty_create(struct fd_table *table, int *master, int *slave);
bool pty_is_object(const struct fd_object *object);
int pty_bytes_available(struct fd_object *object, size_t *available);
int pty_window(struct fd_object *object, struct os_winsize *dimensions, bool set);
int pty_flush_input(struct fd_object *object);
int pty_termios(struct fd_object *object, struct os_termios *attributes, bool set);

#endif
