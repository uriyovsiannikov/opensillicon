#ifndef KERNEL_ROOT_DEVICE_H
#define KERNEL_ROOT_DEVICE_H
#include "kernel/block.h"
/* 0: no root selector, 1: unique UUID match, negative errno: fail closed. */
int root_device_select(const char *cmdline, struct block_device **selected);
#endif
