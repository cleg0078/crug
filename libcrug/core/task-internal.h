/* task-internal.h - runtime-private task hooks (not part of the Crug ABI).
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The channel runtime needs one thing from the task runtime that a program
   must not see: a receive buffer that belongs to the calling task.  A channel
   receive copies through a buffer before returning a pointer to it; if that
   buffer were thread-local, two tasks that share a worker would share it, and
   a task that receives, yields and then reads its value would observe the
   other task's element (docs/crug/CHANNELS.md, docs/crug/TASKS.md).

   It also declares the test seams that force the two runtime interleavings a
   regression test cannot hit reliably: crug_task_start_hook() runs after the
   worker handles are published and before the first thread is created, and
   crug_task_join_hook() runs before each worker is joined.  Both are weak and
   empty in a normal build.

   This header is included by libcrug/core only.  It is deliberately not
   installed and not part of task.h: the ABI between the compiler and libcrug
   stays task.h version 1.  */

#ifndef CRUG_TASK_INTERNAL_H
#define CRUG_TASK_INTERNAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return the calling task's receive buffer, grown to at least need bytes.
   The pointer is owned by the task and valid until the task returns; the next
   call from the same task may move it.  Returns NULL when the caller is not a
   task (the main thread or an arbitrary PAL thread keeps its own buffer).
   Allocation failure is fatal: a receive cannot fail silently.  */
void *crug_task_recv_scratch (size_t need);

/* Test seams for the runtime start/shutdown handshake (S-01).  A test that
   defines either symbol overrides the empty weak definition in task.c.  */
void crug_task_start_hook (void);
void crug_task_join_hook (unsigned index, void *handle);

#ifdef __cplusplus
}
#endif

#endif /* CRUG_TASK_INTERNAL_H */
