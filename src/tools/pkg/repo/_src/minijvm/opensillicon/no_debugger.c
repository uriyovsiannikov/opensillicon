/* SPDX-License-Identifier: MIT */
#include "jdwp.h"

s32 jdwp_start_server(MiniJVM *jvm) { (void)jvm; return -1; }
s32 jdwp_stop_server(MiniJVM *jvm) { (void)jvm; return 0; }
Runtime *jdwp_get_runtime(JdwpServer *srv) { (void)srv; return NULL; }
s32 jdwp_is_ignore_sync(JdwpServer *srv) { (void)srv; return 0; }
void event_on_class_prepare(JdwpServer *srv, Runtime *runtime, JClass *clazz)
{ (void)srv; (void)runtime; (void)clazz; }
void event_on_thread_start(JdwpServer *srv, Instance *thread) { (void)srv; (void)thread; }
void event_on_thread_death(JdwpServer *srv, Instance *thread) { (void)srv; (void)thread; }
