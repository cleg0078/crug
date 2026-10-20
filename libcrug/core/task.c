/* task.c - the Crug task runtime, Phase 14 MVP.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Structure
   ---------
   M tasks run on N operating-system workers, N = min (processors, 8).  The
   workers come from crug_pal_thread_create(), never from a system thread call
   (ADR 0011 D1).  There is one global ready queue, guarded by a small atomic
   spin lock; work stealing, priorities and preemption are deliberately absent
   (ADR 0009 D1).

   A worker loop is: take a task, switch into it, switch back.  A task leaves
   its stack in exactly two ways - crug_task_yield(), which marks it READY and
   switches to the worker that is running it, and returning from its entry,
   which marks it DONE and switches away for good.  The worker, not the task,
   requeues the task; that ordering is what makes the saved stack pointer
   complete before any other worker can observe the task in the queue.

   A task's stack is a PAL reservation: one guard page at the low end (where a
   downward-growing stack would run off), then the committed stack.  The
   lowest committed word holds a canary; crug_task_release() checks it and the
   task magic, and counts a corruption instead of silently reusing the stack.

   The wait/wake protocol: workers block on sched.epoch with
   crug_pal_wait_u32().  Every state change that can make a task runnable, or
   that can let a worker exit, bumps the epoch, so a worker that observed an
   old value is woken and re-tests its predicates.  The queue length itself is
   never a wait predicate, which keeps the abort-on-shutdown path simple.
*/

#include "task.h"

#include "crug_pal.h"
#include "ctx.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRUG_TASK_MAGIC 0xc0dec0dec0dec0deull
#define CRUG_TASK_CANARY 0x5152535455565758ull
#define CRUG_TASK_GUARD_BYTES 4096u
#define CRUG_TASK_WORKERS_DEFAULT_MAX 8u
#define CRUG_TASK_QUEUE_WAKE_ONE 1
#define CRUG_SCOPE_MAGIC 0x5c0be5c0be5c0be5ull

enum crug_task_state
{
  CRUG_TASK_READY = 0,
  CRUG_TASK_RUNNING = 1,
  CRUG_TASK_DONE = 2
};

/* A task group: the pending count of the children that have not returned yet.
   It is incremented before a scoped spawn queues its task and decremented by
   crug_task_finish(); the waiter is woken with crug_pal_wake_all() on the last
   decrement.  */
struct crug_scope
{
  uint64_t magic;
  _Atomic uint32_t pending;
};

struct crug_task
{
  uint64_t magic;
  void *sp;
  struct crug_scope *scope;
  void *stack_reserve;
  size_t stack_reserve_size;
  void *stack_canary;
  size_t frame_size;
  crug_task_entry entry;
  struct crug_task *next;
  _Atomic uint32_t state;
  uint32_t id;
  unsigned char frame[];
};

struct crug_sched
{
  _Atomic uint32_t lock;
  _Atomic uint32_t epoch;
  _Atomic uint32_t shutdown;
  _Atomic uint32_t stopping;
  _Atomic uint32_t started;
  _Atomic uint32_t live;
  _Atomic uint64_t spawned;
  _Atomic uint64_t completed;
  _Atomic uint64_t yielded;
  _Atomic uint64_t corrupt;
  _Atomic uint64_t lost;
  _Atomic uint64_t scopes_entered;
  _Atomic uint64_t scopes_left;
  _Atomic uint32_t scopes_live;
  uint32_t configured_workers;
  uint32_t worker_count;
  uint32_t queue_peak;
  uint32_t queue_length;
  uint32_t next_id;
  uint32_t stack_bytes;
  crug_pal_thread *threads[CRUG_TASK_WORKERS_MAX];
  struct crug_task *head;
  struct crug_task *tail;
};

static struct crug_sched sched;
static _Thread_local struct crug_task *current_task;
static _Thread_local void *worker_sp;

/* The per-worker run queue.  A task is pinned to the worker thread that first
   runs it (ADR 0009 D3, amended in Phase 18): a C task's stack frames may hold
   the address of a thread-local object in a callee-saved register, and
   resuming such a frame on another worker makes it read that other thread's
   storage.  Linux AArch64 and Windows x86-64 both crashed exactly that way
   (current_task read as NULL or as another task's pointer).  The shared queue
   only hands out tasks that have never run; a task that yields goes back to
   its own worker's queue.  These two pointers are touched only by the worker
   loop of this thread and never by task code.  */
static _Thread_local struct crug_task *worker_local_head;
static _Thread_local struct crug_task *worker_local_tail;

static void crug_worker_main (void *arg);

static void crug_runtime_fatal (const char *what) __attribute__ ((noreturn));

static void
crug_runtime_fatal (const char *what)
{
  fprintf (stderr, "crug: task runtime: %s\n", what);
  fflush (stderr);
  abort ();
}

/* ---- ready queue ------------------------------------------------------ */

static void
queue_lock (void)
{
  while (atomic_exchange_explicit (&sched.lock, 1u, memory_order_acquire) != 0)
    {
      /* Short critical sections only; a spin is cheaper than a wait here.  */
    }
}

static void
queue_unlock (void)
{
  atomic_store_explicit (&sched.lock, 0u, memory_order_release);
}

static void
queue_push (struct crug_task *task)
{
  queue_lock ();
  task->next = NULL;
  if (sched.tail == NULL)
    sched.head = task;
  else
    sched.tail->next = task;
  sched.tail = task;
  sched.queue_length++;
  if (sched.queue_length > sched.queue_peak)
    sched.queue_peak = sched.queue_length;
  atomic_fetch_add_explicit (&sched.epoch, 1u, memory_order_release);
  queue_unlock ();
  crug_pal_wake_one (&sched.epoch);
}

static struct crug_task *
queue_pop (void)
{
  struct crug_task *task;

  queue_lock ();
  task = sched.head;
  if (task != NULL)
    {
      sched.head = task->next;
      if (sched.head == NULL)
        sched.tail = NULL;
      sched.queue_length--;
      task->next = NULL;
    }
  queue_unlock ();
  return task;
}

/* The local queue of the calling worker; no lock is needed because only its
   own worker thread touches it.  */

static void
worker_local_push (struct crug_task *task)
{
  task->next = NULL;
  if (worker_local_tail == NULL)
    worker_local_head = task;
  else
    worker_local_tail->next = task;
  worker_local_tail = task;
}

static struct crug_task *
worker_local_pop (void)
{
  struct crug_task *task = worker_local_head;

  if (task != NULL)
    {
      worker_local_head = task->next;
      if (worker_local_head == NULL)
	worker_local_tail = NULL;
      task->next = NULL;
    }
  return task;
}

/* ---- stacks ----------------------------------------------------------- */

static uint32_t
pick_workers (void)
{
  const char *text = getenv ("CRUG_TASKS_WORKERS");
  unsigned long value;

  if (text != NULL && *text != '\0')
    {
      char *end = NULL;
      value = strtoul (text, &end, 10);
      if (end != text && *end == '\0' && value >= 1 && value <= CRUG_TASK_WORKERS_MAX)
        return (uint32_t) value;
      crug_runtime_fatal ("CRUG_TASKS_WORKERS must be an integer between 1 and 64");
    }
  value = (unsigned long) crug_pal_cpu_count ();
  if (value > CRUG_TASK_WORKERS_DEFAULT_MAX)
    value = CRUG_TASK_WORKERS_DEFAULT_MAX;
  if (value < 1)
    value = 1;
  return (uint32_t) value;
}

static uint32_t
pick_stack_bytes (void)
{
  const char *text = getenv ("CRUG_TASKS_STACK");
  unsigned long value;

  if (text != NULL && *text != '\0')
    {
      char *end = NULL;
      value = strtoul (text, &end, 10);
      if (end != text && *end == '\0' && value >= CRUG_TASK_STACK_MIN
          && value <= (16ul * 1024u * 1024u))
        return (uint32_t) value;
      crug_runtime_fatal ("CRUG_TASKS_STACK must be a byte count between 16 KiB and 16 MiB");
    }
  return CRUG_TASK_STACK_DEFAULT;
}

static void
runtime_start (void)
{
  uint32_t workers;
  uint32_t i;

  if (atomic_load_explicit (&sched.started, memory_order_acquire))
    return;
  /* A task may spawn more tasks while the runtime is draining; it must not
     restart the workers (the started flag is still set, and stopping guards
     the window in which the last worker is being joined).  */
  if (atomic_load_explicit (&sched.stopping, memory_order_acquire))
    return;

  queue_lock ();
  if (atomic_load_explicit (&sched.started, memory_order_relaxed)
      || atomic_load_explicit (&sched.stopping, memory_order_relaxed))
    {
      queue_unlock ();
      return;
    }
  workers = pick_workers ();
  sched.configured_workers = workers;
  sched.stack_bytes = pick_stack_bytes ();
  sched.worker_count = workers;
  atomic_store_explicit (&sched.shutdown, 0u, memory_order_release);
  atomic_store_explicit (&sched.started, 1u, memory_order_release);
  queue_unlock ();

  for (i = 0; i < workers; i++)
    {
      if (crug_pal_thread_create (&sched.threads[i], crug_worker_main, NULL) != 0)
        crug_runtime_fatal ("cannot start a worker thread");
    }
}

static void
task_release (struct crug_task *task)
{
  uint64_t canary = 0;

  if (task->stack_canary != NULL)
    memcpy (&canary, task->stack_canary, sizeof canary);
  if (task->magic != CRUG_TASK_MAGIC || canary != CRUG_TASK_CANARY)
    atomic_fetch_add_explicit (&sched.corrupt, 1u, memory_order_relaxed);
  if (task->stack_reserve != NULL)
    crug_pal_memory_release (task->stack_reserve, task->stack_reserve_size);
  task->magic = 0;
  free (task);
}

/* ---- task context ----------------------------------------------------- */

static void crug_task_finish (void) __attribute__ ((noreturn));

static void
crug_task_finish (void)
{
  struct crug_task *task = current_task;

  atomic_store_explicit (&task->state, CRUG_TASK_DONE, memory_order_release);
  atomic_fetch_add_explicit (&sched.completed, 1u, memory_order_relaxed);
  atomic_fetch_sub_explicit (&sched.live, 1u, memory_order_acq_rel);
  atomic_fetch_add_explicit (&sched.epoch, 1u, memory_order_release);

  /* Leave the task group, if any, before the task's stack goes away: the
     group's waiter must observe the decrement after the child has finished
     all of its work (ADR 0024).  */
  if (task->scope != NULL)
    {
      struct crug_scope *scope = task->scope;
      task->scope = NULL;
      if (atomic_fetch_sub_explicit (&scope->pending, 1u, memory_order_acq_rel)
	  == 1u)
	crug_pal_wake_all (&scope->pending);
    }

  if (atomic_load_explicit (&sched.shutdown, memory_order_acquire)
      && atomic_load_explicit (&sched.live, memory_order_acquire) == 0)
    crug_pal_wake_all (&sched.epoch);
  /* Switch to the worker for good; this context is never resumed.  */
  crug_ctx_switch (&task->sp, worker_sp);
  crug_runtime_fatal ("a finished task was resumed");
}

static void
crug_task_trampoline (void)
{
  struct crug_task *task = current_task;

  task->entry (task->frame);
  crug_task_finish ();
  __builtin_unreachable ();
}

void
crug_task_yield (void)
{
  struct crug_task *task = current_task;

  if (task == NULL)
    return; /* the main thread has no task context to yield */

  atomic_store_explicit (&task->state, CRUG_TASK_READY, memory_order_release);
  atomic_fetch_add_explicit (&sched.yielded, 1u, memory_order_relaxed);
  crug_ctx_switch (&task->sp, worker_sp);
}

static struct crug_task *
worker_next_task (void)
{
  for (;;)
    {
      uint32_t epoch = atomic_load_explicit (&sched.epoch, memory_order_acquire);
      struct crug_task *task = queue_pop ();

      /* Fresh tasks come from the shared queue, so that all workers share the
	 load; a resumed task can only come from this worker's own queue.  */
      if (task == NULL)
	task = worker_local_pop ();
      if (task != NULL)
        return task;
      if (atomic_load_explicit (&sched.shutdown, memory_order_acquire)
          && atomic_load_explicit (&sched.live, memory_order_acquire) == 0)
        return NULL;
      crug_pal_wait_u32 (&sched.epoch, epoch);
    }
}

static void
crug_worker_main (void *arg)
{
  (void) arg;

  for (;;)
    {
      struct crug_task *task = worker_next_task ();

      if (task == NULL)
        return;

      atomic_store_explicit (&task->state, CRUG_TASK_RUNNING, memory_order_relaxed);
      current_task = task;
      crug_ctx_switch (&worker_sp, task->sp);
      current_task = NULL;

      if (atomic_load_explicit (&task->state, memory_order_acquire) == CRUG_TASK_DONE)
        task_release (task);
      else
        /* The task stays on this worker: its frames may cache this thread's
	   thread-local addresses (ADR 0009 D3).  */
        worker_local_push (task);
    }
}

/* ---- public interface ------------------------------------------------- */

/* The common body of spawn: SCOPE is the task group the new task joins, or
   NULL for an unstructured task.  */

static void
spawn_common (crug_task_entry entry, const void *frame, size_t frame_size,
	      struct crug_scope *scope)
{
  struct crug_task *task;
  void *reserve;
  size_t total;
  size_t guard;
  size_t stack;
  size_t frame_bytes;

  if (entry == NULL)
    crug_runtime_fatal ("spawn without an entry function");

  runtime_start ();

  guard = CRUG_TASK_GUARD_BYTES;
  stack = sched.stack_bytes;
  total = guard + stack;
  frame_bytes = (frame_size + 15u) & ~(size_t) 15u;

  reserve = crug_pal_memory_reserve (total);
  if (reserve == NULL)
    crug_runtime_fatal ("cannot reserve a task stack");

  if (!crug_pal_memory_commit ((char *) reserve + guard, stack))
    crug_runtime_fatal ("cannot commit a task stack");
  crug_pal_memory_protect_guard (reserve, guard); /* optional capability */

  task = (struct crug_task *) malloc (sizeof *task + frame_bytes);
  if (task == NULL)
    crug_runtime_fatal ("cannot allocate a task");

  task->magic = CRUG_TASK_MAGIC;
  task->stack_reserve = reserve;
  task->stack_reserve_size = total;
  task->stack_canary = (char *) reserve + guard;
  *((uint64_t *) task->stack_canary) = CRUG_TASK_CANARY;
  task->frame_size = frame_size;
  task->entry = entry;
  task->scope = scope;
  task->next = NULL;
  atomic_store_explicit (&task->state, CRUG_TASK_READY, memory_order_relaxed);
  if (frame != NULL && frame_size > 0)
    memcpy (task->frame, frame, frame_size);
  memset (task->frame + frame_size, 0, frame_bytes - frame_size);

  task->sp = crug_ctx_prepare_stack ((char *) reserve + total, crug_task_trampoline);
  if (task->sp == NULL)
    crug_runtime_fatal ("cannot prepare a task stack");

  queue_lock ();
  task->id = ++sched.next_id;
  queue_unlock ();

  atomic_fetch_add_explicit (&sched.live, 1u, memory_order_acq_rel);
  atomic_fetch_add_explicit (&sched.spawned, 1u, memory_order_relaxed);
  queue_push (task);
}

void
crug_task_spawn (crug_task_entry entry, const void *frame, size_t frame_size)
{
  spawn_common (entry, frame, frame_size, NULL);
}

void *
crug_scope_enter (void)
{
  struct crug_scope *scope = (struct crug_scope *) malloc (sizeof *scope);

  if (scope == NULL)
    crug_runtime_fatal ("cannot create a task group");
  scope->magic = CRUG_SCOPE_MAGIC;
  atomic_store_explicit (&scope->pending, 0u, memory_order_relaxed);
  atomic_fetch_add_explicit (&sched.scopes_entered, 1u, memory_order_relaxed);
  atomic_fetch_add_explicit (&sched.scopes_live, 1u, memory_order_acq_rel);
  return scope;
}

void
crug_scope_leave (void *handle)
{
  struct crug_scope *scope = (struct crug_scope *) handle;
  uint32_t pending;

  if (scope == NULL)
    return;
  if (scope->magic != CRUG_SCOPE_MAGIC)
    crug_runtime_fatal ("leave of an invalid task group");

  while ((pending = atomic_load_explicit (&scope->pending,
					  memory_order_acquire)) != 0)
    {
      /* Inside a task a yield gives the workers back; a plain thread waits in
	 the PAL.  Blocking a worker here would starve the very children this
	 thread is waiting for (ADR 0024).  */
      if (current_task != NULL)
	crug_task_yield ();
      else
	crug_pal_wait_u32 (&scope->pending, pending);
    }

  scope->magic = 0;
  free (scope);
  atomic_fetch_add_explicit (&sched.scopes_left, 1u, memory_order_relaxed);
  atomic_fetch_sub_explicit (&sched.scopes_live, 1u, memory_order_acq_rel);
}

void
crug_task_spawn_scoped (void *handle, crug_task_entry entry,
			 const void *frame, size_t frame_size)
{
  struct crug_scope *scope = (struct crug_scope *) handle;

  if (scope == NULL)
    {
      spawn_common (entry, frame, frame_size, NULL);
      return;
    }
  if (scope->magic != CRUG_SCOPE_MAGIC)
    crug_runtime_fatal ("spawn into an invalid task group");

  /* Count the child before it can finish, so the waiter cannot see an empty
     group while a task for it is still being built.  */
  atomic_fetch_add_explicit (&scope->pending, 1u, memory_order_acq_rel);
  spawn_common (entry, frame, frame_size, scope);
}

void
crug_scope_stats_get (struct crug_scope_stats *out)
{
  if (out == NULL)
    return;
  out->entered = atomic_load_explicit (&sched.scopes_entered,
				       memory_order_relaxed);
  out->left = atomic_load_explicit (&sched.scopes_left, memory_order_relaxed);
  out->live = atomic_load_explicit (&sched.scopes_live, memory_order_relaxed);
}

void
crug_task_runtime_shutdown (void)
{
  uint32_t i;
  uint32_t workers;

  if (!atomic_load_explicit (&sched.started, memory_order_acquire))
    return;

  queue_lock ();
  if (!atomic_load_explicit (&sched.started, memory_order_relaxed))
    {
      queue_unlock ();
      return;
    }
  /* started stays set until the workers are joined: a task that is still
     running may spawn more tasks, and those must be queued for the workers
     that are draining, not handed to a second generation of workers.  */
  atomic_store_explicit (&sched.stopping, 1u, memory_order_release);
  atomic_store_explicit (&sched.shutdown, 1u, memory_order_release);
  atomic_fetch_add_explicit (&sched.epoch, 1u, memory_order_release);
  workers = sched.worker_count;
  queue_unlock ();
  crug_pal_wake_all (&sched.epoch);

  for (i = 0; i < workers; i++)
    crug_pal_thread_join (sched.threads[i]);

  queue_lock ();
  sched.worker_count = 0;
  sched.head = NULL;
  sched.tail = NULL;
  sched.queue_length = 0;
  atomic_store_explicit (&sched.started, 0u, memory_order_release);
  atomic_store_explicit (&sched.live, 0u, memory_order_release);
  atomic_store_explicit (&sched.epoch, 0u, memory_order_release);
  atomic_store_explicit (&sched.shutdown, 0u, memory_order_release);
  atomic_store_explicit (&sched.stopping, 0u, memory_order_release);
  queue_unlock ();
  atomic_store_explicit (&sched.lost,
                         atomic_load_explicit (&sched.spawned, memory_order_acquire)
                           - atomic_load_explicit (&sched.completed, memory_order_acquire),
                         memory_order_release);
}

void
crug_task_stats_get (struct crug_task_stats *out)
{
  if (out == NULL)
    return;
  out->spawned = atomic_load_explicit (&sched.spawned, memory_order_acquire);
  out->completed = atomic_load_explicit (&sched.completed, memory_order_acquire);
  out->yielded = atomic_load_explicit (&sched.yielded, memory_order_relaxed);
  out->lost = atomic_load_explicit (&sched.lost, memory_order_relaxed);
  out->stack_corrupt = atomic_load_explicit (&sched.corrupt, memory_order_relaxed);
  out->workers = sched.configured_workers;
  out->queue_peak = sched.queue_peak;
  out->live = atomic_load_explicit (&sched.live, memory_order_acquire);
}

uint32_t
crug_task_current_id (void)
{
  return current_task != NULL ? current_task->id : 0;
}

static void crug_task_atexit (void) __attribute__ ((destructor));

static void
crug_task_atexit (void)
{
  crug_task_runtime_shutdown ();
}
