/* task.h - the Crug task runtime, interface version 1 (Phase 14 MVP).
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The runtime multiplexes M tasks over N operating-system workers (M:N, ADR
   0009).  A task is a stackful fiber: spawn() captures a by-value frame, and
   the worker switches into the task's own stack with the context switch in
   libcrug/arch/x86_64/sysv (ADR 0013).  Tasks are cooperative: they run until
   they return, or until they call crug_task_yield().

   The compiler lowers

     go worker (a, b);

   to a frame struct plus a thunk, then to

     crug_task_spawn (thunk, &frame, sizeof frame);

   so the whole contract the compiler needs is spawn, yield and the stats
   accessor.  A program that spawns tasks should call
   crug_task_runtime_shutdown() at the end of main, or simply return from main:
   a destructor runs the shutdown for it.  The shutdown lets every spawned task
   complete, then joins the workers and releases their stacks.

   Failure model: spawn() has no error return.  A task is guaranteed to be
   queued or the process dies with a diagnostic (out of memory or an operating
   system failure to start a worker).  The MVP has no task cancellation, no
   priorities and no preemption.
*/

#ifndef CRUG_TASK_H
#define CRUG_TASK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default fixed stack of one task, in bytes; override with
   CRUG_TASKS_STACK=<bytes> before the first spawn.  Stacks never grow in the
   MVP (ADR 0008): a task that needs more runs on more stack or on the heap.  */
#define CRUG_TASK_STACK_DEFAULT (64u * 1024u)
#define CRUG_TASK_STACK_MIN (16u * 1024u)

/* Default worker count: min (usable processors, 8), at least 1; override with
   CRUG_TASKS_WORKERS=<1..64> before the first spawn.  */
#define CRUG_TASK_WORKERS_MAX 64u

/* The entry shape the compiler's thunk uses: one pointer to its copied frame.  */
typedef void (*crug_task_entry) (void *frame);

/* Queue one task.  frame is copied before this returns, so the caller's stack
   frame may die immediately; frame_size may be 0 for a frame-less task.  */
void crug_task_spawn (crug_task_entry entry, const void *frame, size_t frame_size);

/* Let other tasks run.  A no-op outside a task (on the main thread).  */
void crug_task_yield (void);

/* Idempotent: run every remaining task, join the workers, release the stacks.
   A later spawn starts the runtime again.  */
void crug_task_runtime_shutdown (void);

struct crug_task_stats
{
  uint64_t spawned;        /* tasks accepted by spawn                     */
  uint64_t completed;      /* tasks whose entry returned                  */
  uint64_t yielded;        /* voluntary yields                            */
  uint64_t lost;           /* spawned - completed at the last shutdown    */
  uint64_t stack_corrupt;  /* tasks whose stack guard or magic was broken */
  uint32_t workers;        /* configured worker count                     */
  uint32_t queue_peak;     /* high-water mark of the ready queue          */
  uint32_t live;           /* spawned but not yet completed               */
};

void crug_task_stats_get (struct crug_task_stats *out);

/* The calling task's identifier (0 on the main thread).  */
uint32_t crug_task_current_id (void);

#ifdef __cplusplus
}
#endif

#endif /* CRUG_TASK_H */
