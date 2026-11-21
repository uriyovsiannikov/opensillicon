/* SPDX-License-Identifier: MIT */
#ifndef OPEN_SILLICON_THREADS_H
#define OPEN_SILLICON_THREADS_H
/* Periodic Java backedge polling; explicit Thread.yield and waits stay eager. */
void minijvm_cooperative_yield(void);
#endif
