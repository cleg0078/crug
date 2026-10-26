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

/* Run every remaining task, join the workers, release the stacks.  A later
   spawn starts the runtime again.  The void form is part of ABI version 1 and
   stays void: a call from a task, or a worker that cannot be joined, is a
   fatal diagnostic rather than a silent half-shutdown.  */
void crug_task_runtime_shutdown (void);

/* The checked form of the shutdown above.  Returns
     0  this call stopped the runtime;
     1  there was nothing left to stop: the runtime was not started, or an
	 overlapping caller stopped it while this call waited;
     2  the call came from a task (nothing changed);
     3  a worker could not be joined (nothing else changed; a later call
	 reports 3 as well).
   Idempotent and safe from several threads at once: one caller owns the
   shutdown and an overlapping caller waits for it.  A task may not call the
   void form: status 2 there is fatal.  */
int crug_task_runtime_try_shutdown (void);

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


/* ---- task groups (the runtime half of 'scope') ------------------------- */

/* A scope statement is a task group: the group is created when the block is
   entered, every scoped spawn joins it, and leaving the block waits until the
   group is empty.  The compiler lowers

     scope { go worker (a); }

   to crug_scope_enter (), the body, crug_task_spawn_scoped (group, ...) for
   each child, and crug_scope_leave (group) on every path out of the block.
   The group is opaque: only the two calls below may touch the handle.
   Leaving a group from a task yields, from the main thread it waits in the
   PAL, so the handle never blocks a worker while it waits (ADR 0024).  */

/* Create a group.  Never returns NULL: creating a group is not allowed to
   fail silently (the process dies with a diagnostic instead).  */
void *crug_scope_enter (void);

/* Wait for every child of the group, then release it.  A NULL handle is a
   no-op, so a scope whose entry was skipped by a goto still leaves cleanly;
   the compiler rejects such a goto instead.  */
void crug_scope_leave (void *scope);

/* Queue one task as a child of the group: the group's pending count rises
   until the task returns.  A NULL group is crug_task_spawn ().  */
void crug_task_spawn_scoped (void *scope, crug_task_entry entry,
			     const void *frame, size_t frame_size);

struct crug_scope_stats
{
  uint64_t entered;  /* groups created                                   */
  uint64_t left;     /* groups released                                  */
  uint64_t live;     /* groups created but not released                  */
};

void crug_scope_stats_get (struct crug_scope_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* CRUG_TASK_H */
