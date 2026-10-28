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

#include "task-internal.h"

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
#define CRUG_STACK_CACHE_DEFAULT 64u
#define CRUG_STACK_CACHE_LIMIT 65536u
#define CRUG_TASK_CACHE_DEFAULT 256u
#define CRUG_TASK_CACHE_LIMIT 65536u
#define CRUG_TASK_CACHE_BINS 8u
#define CRUG_STACK_LOCAL 32u
#define CRUG_TASK_QUEUE_WAKE_ONE 1
#define CRUG_TASK_SHUTDOWN_POLL_NS 1000000ull  /* 1 ms between status checks */
#define CRUG_TASK_SHUTDOWN_POLLS 60000u        /* at most 60 s for a waiter  */
#define CRUG_TASK_RECV_SCRATCH_CACHE_LIMIT (64u * 1024u)
#define CRUG_TASK_RECV_SCRATCH_ALIGN 16u
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
   decrement.

   pending alone cannot say when the group may be freed: the last child drops
   it and then calls crug_pal_wake_all (&pending), so a waiter that observes
   pending zero could free the group while that wake is still using its
   address (R-01).  in_flight is the completion handshake that closes the
   window: a finishing child raises it before it drops pending and lowers it
   only after the wake has returned, and crug_scope_leave() frees the group
   only once both counters are zero.  */
struct crug_scope
{
  uint64_t magic;
  _Atomic uint32_t pending;
  _Atomic uint32_t in_flight;
};

/* One reusable stack slot.  Slots live in a small global cache so a spawn does
   not reserve, commit and protect a private stack every time.  The canary is
   reset before a slot is reused; a slot whose canary was overwritten is
   released instead of cached (ADR 0008 A-2).  */
struct crug_stack
{
  void *reserve;
  size_t total;
  void *top;
  void *canary;
  struct crug_stack *next;
};

struct crug_task
{
  uint64_t magic;
  void *sp;
  struct crug_scope *scope;
  struct crug_stack *stack;
  size_t frame_size;
  size_t alloc_size;
  crug_task_entry entry;
  struct crug_task *next;
  struct crug_task *cache_next;
  _Atomic uint32_t state;
  uint32_t id;
  /* The channel receive buffer of this task (task-internal.h).  It is lazy:
     freed with the task, and never shared with another task.  recv_scratch is
     the aligned pointer handed to the runtime; recv_scratch_base is the
     pointer malloc returned and therefore the one to free.  A buffer larger
     than CRUG_TASK_RECV_SCRATCH_CACHE_LIMIT is returned to malloc when the
     task finishes instead of being cached with it (A-01).  */
  void *recv_scratch;
  void *recv_scratch_base;
  size_t recv_scratch_size;
  /* The captured frame the compiler's thunk reads.  It must be aligned for
     every object type the language can capture, so the offset is rounded up
     to max_align_t (spawn_common sizes the allocation the same way).  */
  _Alignas (max_align_t) unsigned char frame[];
};

_Static_assert (offsetof (struct crug_task, frame) % _Alignof (max_align_t) == 0,
		"the task frame offset must be max_align_t aligned");

struct crug_sched
{
  _Atomic uint32_t lock;
  _Atomic uint32_t epoch;
  _Atomic uint32_t shutdown;
  _Atomic uint32_t stopping;
  _Atomic uint32_t sleepers;
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
  /* Set when the caller that owns a shutdown could not join a worker.  A
     later caller then reports the same failure at once instead of waiting for
     the 60 s overlap timeout.  */
  _Atomic uint32_t stop_failed;
  /* 0 = idle, 1 = a start is publishing the worker handles, 2 = started
     (S-01).  A shutdown must not claim the runtime while a start is still in
     the 1 window, or it would join slots that do not hold a handle yet.  */
  _Atomic int start_state;
  uint32_t configured_workers;
  uint32_t worker_count;
  /* Written under the queue lock, read without it by crug_task_stats_get(),
     so it is atomic even though queue_length stays lock-protected (R-02).  */
  _Atomic uint32_t queue_peak;
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
static _Thread_local struct crug_stack *local_stack_free;
static _Thread_local uint32_t local_stack_count;

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

/* Test seams, declared in task-internal.h.  They are weak and empty in a
   normal build; a test can override them to pause the runtime between the
   publication of the worker handles and the first thread creation
   (crug_task_start_hook), or to inspect a join from inside the shutdown
   (crug_task_join_hook).  Neither is on a hot path: one call per runtime
   start and one per joined worker.  */
void crug_task_start_hook (void) __attribute__ ((weak));
void crug_task_join_hook (unsigned index, void *handle) __attribute__ ((weak));

void
crug_task_start_hook (void)
{
}

void
crug_task_join_hook (unsigned index, void *handle)
{
  (void) index;
  (void) handle;
}


/* ---- allocation caches ------------------------------------------------ */

struct crug_task_bin
{
  _Atomic uint32_t lock;
  struct crug_task *free;
  uint32_t count;
  uint32_t limit;
};

struct crug_stack_cache
{
  _Atomic uint32_t lock;
  struct crug_stack *free;
  uint32_t count;
  uint32_t limit;
};

static struct crug_stack_cache stack_cache;
static struct crug_task_bin task_bins[CRUG_TASK_CACHE_BINS];
static _Atomic uint32_t stack_cache_ready;
static _Atomic uint32_t task_cache_ready;

static const size_t task_class_sizes[CRUG_TASK_CACHE_BINS] =
{ 128u, 256u, 512u, 1024u, 2048u, 4096u, 8192u, 16384u };

static void
spin_lock (_Atomic uint32_t *lock)
{
  while (atomic_exchange_explicit (lock, 1u, memory_order_acquire) != 0)
    ;
}

static void
spin_unlock (_Atomic uint32_t *lock)
{
  atomic_store_explicit (lock, 0u, memory_order_release);
}

static uint32_t
pick_cache_limit (const char *name, uint32_t fallback, uint32_t maximum)
{
  const char *text = getenv (name);
  unsigned long value;
  char *end = NULL;

  if (text == NULL || *text == '\0')
    return fallback;
  value = strtoul (text, &end, 10);
  if (end == text || *end != '\0' || value > maximum)
    crug_runtime_fatal ("a task cache limit is out of range");
  return (uint32_t) value;
}

static int
task_class_index (size_t total)
{
  uint32_t i;

  for (i = 0; i < CRUG_TASK_CACHE_BINS; i++)
    if (total <= task_class_sizes[i])
      return (int) i;
  return -1;
}

static void
task_cache_init (void)
{
  uint32_t limit;
  uint32_t i;

  if (atomic_load_explicit (&task_cache_ready, memory_order_acquire))
    return;
  limit = pick_cache_limit ("CRUG_TASKS_TASK_CACHE", CRUG_TASK_CACHE_DEFAULT,
			    CRUG_TASK_CACHE_LIMIT);
  for (i = 0; i < CRUG_TASK_CACHE_BINS; i++)
    {
      atomic_store_explicit (&task_bins[i].lock, 0u, memory_order_relaxed);
      task_bins[i].free = NULL;
      task_bins[i].count = 0;
      task_bins[i].limit = limit;
    }
  atomic_store_explicit (&task_cache_ready, 1u, memory_order_release);
}

static void
stack_cache_init (void)
{
  if (atomic_load_explicit (&stack_cache_ready, memory_order_acquire))
    return;
  atomic_store_explicit (&stack_cache.lock, 0u, memory_order_relaxed);
  stack_cache.free = NULL;
  stack_cache.count = 0;
  stack_cache.limit = pick_cache_limit ("CRUG_TASKS_CACHE",
				       CRUG_STACK_CACHE_DEFAULT,
				       CRUG_STACK_CACHE_LIMIT);
  atomic_store_explicit (&stack_cache_ready, 1u, memory_order_release);
}

static void stack_release (struct crug_stack *stack);

static struct crug_stack *
stack_new (void)
{
  struct crug_stack *stack;
  void *reserve;
  size_t guard;
  size_t bytes;
  size_t total;

  guard = crug_pal_page_size ();
  if (guard < CRUG_TASK_GUARD_BYTES)
    guard = CRUG_TASK_GUARD_BYTES;
  bytes = sched.stack_bytes;
  bytes = (bytes + guard - 1u) & ~(guard - 1u);
  total = guard + bytes;
  reserve = crug_pal_memory_reserve (total);
  if (reserve == NULL)
    crug_runtime_fatal ("cannot reserve a task stack");
  if (!crug_pal_memory_commit ((char *) reserve + guard, bytes))
    crug_runtime_fatal ("cannot commit a task stack");
  crug_pal_memory_protect_guard (reserve, guard);
  stack = (struct crug_stack *) malloc (sizeof *stack);
  if (stack == NULL)
    {
      crug_pal_memory_release (reserve, total);
      crug_runtime_fatal ("cannot allocate a task stack slot");
    }
  stack->reserve = reserve;
  stack->total = total;
  stack->top = (char *) reserve + total;
  stack->canary = (char *) reserve + guard;
  *((uint64_t *) stack->canary) = CRUG_TASK_CANARY;
  stack->next = NULL;
  return stack;
}

static void
stack_release (struct crug_stack *stack)
{
  if (stack == NULL)
    return;
  crug_pal_memory_release (stack->reserve, stack->total);
  free (stack);
}

static void
stack_local_flush (void)
{
  struct crug_stack *stack = local_stack_free;
  struct crug_stack *drop = NULL;

  local_stack_free = NULL;
  local_stack_count = 0;
  if (stack == NULL)
    return;
  if (!atomic_load_explicit (&stack_cache_ready, memory_order_acquire)
      || stack_cache.limit == 0u)
    {
      while (stack != NULL)
	{
	  struct crug_stack *next = stack->next;
	  stack_release (stack);
	  stack = next;
	}
      return;
    }
  spin_lock (&stack_cache.lock);
  while (stack != NULL)
    {
      struct crug_stack *next = stack->next;
      if (stack_cache.count < stack_cache.limit)
	{
	  stack->next = stack_cache.free;
	  stack_cache.free = stack;
	  stack_cache.count++;
	}
      else
	{
	  stack->next = drop;
	  drop = stack;
	}
      stack = next;
    }
  spin_unlock (&stack_cache.lock);
  while (drop != NULL)
    {
      struct crug_stack *next = drop->next;
      stack_release (drop);
      drop = next;
    }
}

static struct crug_stack *
stack_alloc (void)
{
  struct crug_stack *stack;

  if (!atomic_load_explicit (&stack_cache_ready, memory_order_acquire))
    stack_cache_init ();
  if (stack_cache.limit != 0u && local_stack_count != 0u)
    {
      stack = local_stack_free;
      local_stack_free = stack->next;
      local_stack_count--;
      stack->next = NULL;
      *((uint64_t *) stack->canary) = CRUG_TASK_CANARY;
      return stack;
    }
  if (stack_cache.limit == 0u)
    return stack_new ();

  spin_lock (&stack_cache.lock);
  while (stack_cache.free != NULL && local_stack_count < CRUG_STACK_LOCAL)
    {
      stack = stack_cache.free;
      stack_cache.free = stack->next;
      stack_cache.count--;
      stack->next = local_stack_free;
      local_stack_free = stack;
      local_stack_count++;
    }
  spin_unlock (&stack_cache.lock);
  if (local_stack_count != 0u)
    {
      stack = local_stack_free;
      local_stack_free = stack->next;
      local_stack_count--;
      stack->next = NULL;
      *((uint64_t *) stack->canary) = CRUG_TASK_CANARY;
      return stack;
    }
  return stack_new ();
}

static void
stack_put (struct crug_stack *stack)
{
  if (stack == NULL)
    return;
  if (!atomic_load_explicit (&stack_cache_ready, memory_order_acquire)
      || stack_cache.limit == 0u)
    {
      stack_release (stack);
      return;
    }
  if (local_stack_count < CRUG_STACK_LOCAL)
    {
      stack->next = local_stack_free;
      local_stack_free = stack;
      local_stack_count++;
      return;
    }
  stack_local_flush ();
  stack->next = local_stack_free;
  local_stack_free = stack;
  local_stack_count = 1;
}

static bool
stack_is_intact (struct crug_stack *stack)
{
  uint64_t canary = 0;

  if (stack == NULL || stack->canary == NULL)
    return false;
  memcpy (&canary, stack->canary, sizeof canary);
  return canary == CRUG_TASK_CANARY;
}

static void
stack_cache_flush (void)
{
  struct crug_stack *stack;

  if (!atomic_load_explicit (&stack_cache_ready, memory_order_acquire))
    return;
  spin_lock (&stack_cache.lock);
  stack = stack_cache.free;
  stack_cache.free = NULL;
  stack_cache.count = 0;
  spin_unlock (&stack_cache.lock);
  while (stack != NULL)
    {
      struct crug_stack *next = stack->next;
      stack_release (stack);
      stack = next;
    }
  atomic_store_explicit (&stack_cache_ready, 0u, memory_order_release);
}

static struct crug_task *
task_alloc (size_t total)
{
  struct crug_task *task;
  size_t size = total;
  int bin;

  if (!atomic_load_explicit (&task_cache_ready, memory_order_acquire))
    task_cache_init ();
  bin = task_class_index (total);
  if (bin >= 0)
    size = task_class_sizes[bin];
  if (bin < 0 || task_bins[bin].limit == 0u)
    {
      task = (struct crug_task *) malloc (size);
      if (task != NULL)
	{
	  task->alloc_size = size;
	  task->recv_scratch = NULL;
	  task->recv_scratch_base = NULL;
	  task->recv_scratch_size = 0;
	}
      return task;
    }
  spin_lock (&task_bins[bin].lock);
  task = task_bins[bin].free;
  if (task != NULL)
    {
      task_bins[bin].free = task->cache_next;
      task_bins[bin].count--;
    }
  spin_unlock (&task_bins[bin].lock);
  if (task == NULL)
    {
      task = (struct crug_task *) malloc (size);
      if (task != NULL)
	{
	  task->alloc_size = size;
	  task->recv_scratch = NULL;
	  task->recv_scratch_base = NULL;
	  task->recv_scratch_size = 0;
	}
    }
  return task;
}

static void
task_free (struct crug_task *task)
{
  int bin;

  if (task == NULL)
    return;
  /* A large receive buffer is not cached with the task: the task cache holds
     the whole struct until the runtime shuts down, so one large element
     received in a task would pin its buffer for the lifetime of the cache
     (A-01).  Buffers up to the limit still ride along with the task.  */
  if (task->recv_scratch_base != NULL
      && task->recv_scratch_size > CRUG_TASK_RECV_SCRATCH_CACHE_LIMIT)
    {
      free (task->recv_scratch_base);
      task->recv_scratch = NULL;
      task->recv_scratch_base = NULL;
      task->recv_scratch_size = 0;
    }
  bin = task_class_index (task->alloc_size);
  if (bin >= 0 && task_bins[bin].limit != 0u)
    {
      spin_lock (&task_bins[bin].lock);
      if (task_bins[bin].count < task_bins[bin].limit)
	{
	  task->cache_next = task_bins[bin].free;
	  task_bins[bin].free = task;
	  task_bins[bin].count++;
	  spin_unlock (&task_bins[bin].lock);
	  return;
	}
      spin_unlock (&task_bins[bin].lock);
    }
  free (task->recv_scratch_base);
  free (task);
}

static void
task_cache_flush (void)
{
  uint32_t i;

  if (!atomic_load_explicit (&task_cache_ready, memory_order_acquire))
    return;
  for (i = 0; i < CRUG_TASK_CACHE_BINS; i++)
    {
      struct crug_task *task;

      spin_lock (&task_bins[i].lock);
      task = task_bins[i].free;
      task_bins[i].free = NULL;
      task_bins[i].count = 0;
      spin_unlock (&task_bins[i].lock);
      while (task != NULL)
	{
	  struct crug_task *next = task->cache_next;
	  free (task->recv_scratch_base);
	  free (task);
	  task = next;
	}
    }
  atomic_store_explicit (&task_cache_ready, 0u, memory_order_release);
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

/* Append TASK; the caller holds the scheduler lock.  spawn_common needs the
   append to happen in the same critical section as its admission check, so
   that a shutdown cannot start between "the task is admitted" and "the task
   is visible to the workers".  */
static void
queue_push_locked (struct crug_task *task)
{
  task->next = NULL;
  if (sched.tail == NULL)
    sched.head = task;
  else
    sched.tail->next = task;
  sched.tail = task;
  sched.queue_length++;
  if (sched.queue_length
      > atomic_load_explicit (&sched.queue_peak, memory_order_relaxed))
    atomic_store_explicit (&sched.queue_peak, sched.queue_length,
			   memory_order_relaxed);
  atomic_fetch_add_explicit (&sched.epoch, 1u, memory_order_release);
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

/* Start the workers.  Returns true when this call created them, false when the
   runtime was already started (or is stopping).  The caller uses the result to
   publish the completed start in its own spawn admission (S-01): publishing
   from here would open a window in which a shutdown could claim the runtime
   between the start and the starter's own admission.  */
static int
runtime_start (void)
{
  uint32_t workers;
  uint32_t i;

  if (atomic_load_explicit (&sched.started, memory_order_acquire))
    return 0;
  /* A task may spawn more tasks while the runtime is draining; it must not
     restart the workers (the started flag is still set, and stopping guards
     the window in which the last worker is being joined).  */
  if (atomic_load_explicit (&sched.stopping, memory_order_acquire))
    return 0;

  queue_lock ();
  if (atomic_load_explicit (&sched.started, memory_order_relaxed)
      || atomic_load_explicit (&sched.stopping, memory_order_relaxed))
    {
      queue_unlock ();
      return 0;
    }
  workers = pick_workers ();
  sched.configured_workers = workers;
  sched.stack_bytes = pick_stack_bytes ();
  stack_cache_init ();
  task_cache_init ();
  atomic_store_explicit (&sched.sleepers, 0u, memory_order_relaxed);
  sched.worker_count = workers;
  atomic_store_explicit (&sched.shutdown, 0u, memory_order_release);
  atomic_store_explicit (&sched.started, 1u, memory_order_release);
  /* The start is in progress until the starter publishes 2 from its spawn
     admission; until then a shutdown waits for the handles to exist.  */
  atomic_store_explicit (&sched.start_state, 1, memory_order_relaxed);
  queue_unlock ();

  crug_task_start_hook ();

  for (i = 0; i < workers; i++)
    {
      if (crug_pal_thread_create (&sched.threads[i], crug_worker_main, NULL) != 0)
	{
	  /* Never leave start_state at 1: a stop that is waiting for the
	     publication must not hang over a fatal start failure.  */
	  atomic_store_explicit (&sched.start_state, 2, memory_order_release);
	  crug_runtime_fatal ("cannot start a worker thread");
	}
    }
  return 1;
}

static void
task_release (struct crug_task *task)
{
  /* A task released before it ran never materialized a stack; it is intact by
     construction (nothing executed on it, so nothing can have overrun it).  */
  bool intact = task->magic == CRUG_TASK_MAGIC
		&& (task->stack == NULL || stack_is_intact (task->stack));

  if (!intact)
    {
      atomic_fetch_add_explicit (&sched.corrupt, 1u, memory_order_relaxed);
      stack_release (task->stack);
      free (task->recv_scratch_base);
      free (task);
      return;
    }
  stack_put (task->stack);
  task->magic = 0;
  task_free (task);
}

/* The per-task receive buffer the channel runtime copies through; see
   task-internal.h.  A NULL current_task means the main thread (or an
   arbitrary PAL thread), which keeps its own thread-local buffer instead.

   The buffer must satisfy the alignment its caller asks for: malloc and
   realloc only promise _Alignof (max_align_t), so an over-aligned element
   (C-01) needs a manually rounded allocation.  The raw pointer is kept in
   recv_scratch_base for free(); the aligned pointer in recv_scratch is what
   callers see.  An existing buffer is reused only when it is large enough
   and already aligned for this request.  */
void *
crug_task_recv_scratch_aligned (size_t size, size_t align)
{
  struct crug_task *task = current_task;
  void *base;
  void *aligned;
  size_t extra;

  if (align == 0u || (align & (align - 1u)) != 0u)
    crug_runtime_fatal ("invalid task receive buffer alignment");
  if (task == NULL)
    return NULL;
  if (task->recv_scratch != NULL && task->recv_scratch_size >= size
      && (uintptr_t) task->recv_scratch % align == 0u)
    return task->recv_scratch;
  if (size > SIZE_MAX - (align - 1u))
    crug_runtime_fatal ("cannot allocate a task receive buffer");
  extra = size + align - 1u;
  base = malloc (extra);
  if (base == NULL)
    crug_runtime_fatal ("cannot allocate a task receive buffer");
  aligned = (void *) (((uintptr_t) base + align - 1u)
		      & ~(uintptr_t) (align - 1u));
  free (task->recv_scratch_base);
  task->recv_scratch_base = base;
  task->recv_scratch = aligned;
  task->recv_scratch_size = size;
  return aligned;
}

/* The old entry point: the shared receive buffer only needs fundamental
   alignment, because the compiler now receives an over-aligned element into
   a temporary of its own type through crug_chan_recv_into() (C-01).  */
void *
crug_task_recv_scratch (size_t need)
{
  return crug_task_recv_scratch_aligned (need, CRUG_TASK_RECV_SCRATCH_ALIGN);
}

/* ---- task context ----------------------------------------------------- */

static void crug_task_finish (void) __attribute__ ((noreturn));

static void
crug_task_finish (void)
{
  struct crug_task *task = current_task;

  atomic_store_explicit (&task->state, CRUG_TASK_DONE, memory_order_release);
  atomic_fetch_add_explicit (&sched.completed, 1u, memory_order_relaxed);
  /* A task was admitted by spawn_common, so live is at least one while it
     runs.  A zero means the task was never admitted (or a finish ran twice):
     that is a runtime invariant violation, not a counter to wrap.  */
  for (;;)
    {
      uint32_t live = atomic_load_explicit (&sched.live, memory_order_acquire);

      if (live == 0u)
	{
	  atomic_fetch_add_explicit (&sched.corrupt, 1u, memory_order_relaxed);
	  crug_runtime_fatal ("a task finished without a spawn admission");
	}
      if (atomic_compare_exchange_weak_explicit (&sched.live, &live, live - 1u,
						 memory_order_acq_rel,
						 memory_order_acquire))
	break;
    }
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
      atomic_fetch_add_explicit (&sched.sleepers, 1u, memory_order_acq_rel);
      crug_pal_wait_u32 (&sched.epoch, epoch);
      atomic_fetch_sub_explicit (&sched.sleepers, 1u, memory_order_acq_rel);
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
	{
	  stack_local_flush ();
	  return;
	}

      atomic_store_explicit (&task->state, CRUG_TASK_RUNNING, memory_order_relaxed);
      current_task = task;
      /* A spawned task arrives without a stack: spawn_common queues it and the
	 worker that will run it materializes the stack here.  A burst of queued
	 tasks therefore costs no PAL address space and no guard page until the
	 scheduler reaches it (ADR 0008 A-3).  stack_new either returns a stack or
	 dies with a diagnostic, so no error path is needed.  */
      if (task->stack == NULL)
	{
	  task->stack = stack_alloc ();
	  task->sp = crug_ctx_prepare_stack (task->stack->top,
					     crug_task_trampoline);
	  if (task->sp == NULL)
	    crug_runtime_fatal ("cannot prepare a task stack");
	}
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
  size_t frame_bytes;
  size_t align;
  int started_here;

  if (entry == NULL)
    crug_runtime_fatal ("spawn without an entry function");

  started_here = runtime_start ();

  /* The compiler copies a by-value frame into task->frame and the thunk reads
     it as the captured structure, so the offset must satisfy the strictest
     alignment an object can require.  Both size computations are checked: the
     frontend rejects an over-aligned capture, but / sizeof arithmetic that
     wraps would otherwise reserve a tiny block for a large frame.  */
  align = _Alignof (max_align_t);
  if (frame_size > SIZE_MAX - (align - 1u))
    crug_runtime_fatal ("crug-CONC-028: the task frame size overflows");
  frame_bytes = (frame_size + (align - 1u)) & ~(align - 1u);
  if (sizeof *task > SIZE_MAX - frame_bytes)
    crug_runtime_fatal ("crug-CONC-028: the task frame size overflows");

  task = task_alloc (sizeof *task + frame_bytes);
  if (task == NULL)
    crug_runtime_fatal ("cannot allocate a task");

  task->magic = CRUG_TASK_MAGIC;
  /* No stack yet: the worker that dispatches the task materializes it
     (ADR 0008 A-3), so a queued task costs only its frame and the runtime
     pins no PAL address space per pending task.  */
  task->stack = NULL;
  task->sp = NULL;
  task->frame_size = frame_size;
  task->entry = entry;
  task->scope = scope;
  task->next = NULL;
  task->cache_next = NULL;
  atomic_store_explicit (&task->state, CRUG_TASK_READY, memory_order_relaxed);
  if (frame != NULL && frame_size > 0)
    memcpy (task->frame, frame, frame_size);
  memset (task->frame + frame_size, 0, frame_bytes - frame_size);

  /* Admission is one critical section: the shutdown state is read, the live
     count is raised, the identifier is taken and the task becomes visible to
     the workers together.  A spawn that races a shutdown dies with the
     diagnostic instead of joining a runtime that is draining (ADR 0009).  */
  queue_lock ();
  if (atomic_load_explicit (&sched.stopping, memory_order_acquire)
      || atomic_load_explicit (&sched.shutdown, memory_order_acquire))
    {
      /* The starter's own admission cannot lose this race any more (a stop
	 waits for start_state != 1), but publish the completion anyway so a
	 stop can never hang behind a dying starter.  */
      if (started_here)
	atomic_store_explicit (&sched.start_state, 2, memory_order_release);
      queue_unlock ();
      crug_runtime_fatal ("crug-CONC-029: cannot spawn during shutdown");
    }
  task->id = ++sched.next_id;
  atomic_fetch_add_explicit (&sched.live, 1u, memory_order_acq_rel);
  atomic_fetch_add_explicit (&sched.spawned, 1u, memory_order_relaxed);
  queue_push_locked (task);
  /* Publish the completed start only now, in the very critical section that
     admits the starter's own task: a shutdown that waits for start_state != 1
     can then never claim the runtime before this task is visible, while the
     handles it will join are guaranteed to be published (S-01).  */
  if (started_here)
    atomic_store_explicit (&sched.start_state, 2, memory_order_release);
  queue_unlock ();
  if (started_here)
    /* The workers this start just created may already be asleep on the epoch
       counter, so wake them all instead of a single sleeper.  */
    crug_pal_wake_all (&sched.epoch);
  else if (atomic_load_explicit (&sched.sleepers, memory_order_acquire) != 0u)
    crug_pal_wake_one (&sched.epoch);
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
  atomic_store_explicit (&scope->in_flight, 0u, memory_order_relaxed);
  atomic_fetch_add_explicit (&sched.scopes_entered, 1u, memory_order_relaxed);
  atomic_fetch_add_explicit (&sched.scopes_live, 1u, memory_order_acq_rel);
  return scope;
}

void
crug_scope_leave (void *handle)
{
  struct crug_scope *scope = (struct crug_scope *) handle;
  uint32_t pending;
  uint32_t in_flight;

  if (scope == NULL)
    return;
  if (scope->magic != CRUG_SCOPE_MAGIC)
    crug_runtime_fatal ("leave of an invalid task group");

  for (;;)
    {
      pending = atomic_load_explicit (&scope->pending, memory_order_acquire);
      in_flight = atomic_load_explicit (&scope->in_flight, memory_order_acquire);
      /* pending zero is not enough: the last child is still inside its
	 completion wake until it drops in_flight, so the group must stay alive
	 until both are zero (R-01).  */
      if (pending == 0u && in_flight == 0u)
	break;
      if (current_task != NULL)
	{
	  /* Inside a task a yield gives the workers back; blocking a worker
	     here would starve the very children this thread waits for
	     (ADR 0024).  */
	  crug_task_yield ();
	}
      else if (pending != 0u)
	{
	  /* A plain thread sleeps in the PAL until the last child drops
	     pending; the wake it is waiting for is the one in_flight protects.  */
	  crug_pal_wait_u32 (&scope->pending, pending);
	}
      else
	{
	  /* Only the completion wake is still running.  It does not post a
	     PAL wake for in_flight, so poll it; the window is a single wake
	     call, far shorter than this sleep.  */
	  crug_pal_sleep_ns (CRUG_TASK_SHUTDOWN_POLL_NS);
	}
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

int
crug_task_runtime_try_shutdown (void)
{
  uint32_t i;
  uint32_t workers;
  unsigned polls;

  /* Checked first and before any mutation: a task cannot join the workers it
     is running on, so the answer must be defined even while another thread
     owns the shutdown.  The void entry point below turns this status into a
     diagnostic, which is how ABI version 1 stays void.  */
  if (current_task != NULL)
    return 2;

  if (!atomic_load_explicit (&sched.started, memory_order_acquire))
    return 1;

  /* S-01: while start_state is 1 the starter is still creating the worker
     threads, so the handles this function is about to join may not exist yet.
     Wait for the publication instead of claiming the shutdown and joining an
     unpublished slot (which used to yield a spurious status 3 and leave the
     runtime neither stopped nor startable).

     This poll cannot deadlock: the only writer of 1 -> 2 is the thread that
     called runtime_start(), and that thread either reaches its spawn admission
     (which publishes 2) or terminates the process in crug_runtime_fatal.  No
     other thread can hold the runtime in the 1 state, and this thread has not
     claimed anything yet, so it blocks nothing.  The sleep keeps it off the
     CPU for the (very short) duration of the start.  */
  while (atomic_load_explicit (&sched.start_state, memory_order_acquire) == 1)
    crug_pal_sleep_ns (CRUG_TASK_SHUTDOWN_POLL_NS);

  queue_lock ();
  if (!atomic_load_explicit (&sched.started, memory_order_relaxed))
    {
      queue_unlock ();
      return 1;
    }
  if (atomic_load_explicit (&sched.stopping, memory_order_relaxed))
    {
      queue_unlock ();
      /* Another caller owns the shutdown.  Wait for it, bounded, so an
	 overlapping caller reports the real outcome instead of a spurious
	 "not started" while the workers are still being joined.  */
      for (polls = 0; polls < CRUG_TASK_SHUTDOWN_POLLS; polls++)
	{
	  if (atomic_load_explicit (&sched.stop_failed, memory_order_acquire))
	    return 3;
	  if (!atomic_load_explicit (&sched.started, memory_order_acquire))
	    return 1;
	  crug_pal_sleep_ns (CRUG_TASK_SHUTDOWN_POLL_NS);
	}
      return 3;
    }
  /* started stays set while the workers drain: spawn admission uses stopping
     to reject a task that would otherwise join a runtime that is leaving.  */
  atomic_store_explicit (&sched.stopping, 1u, memory_order_release);
  atomic_store_explicit (&sched.shutdown, 1u, memory_order_release);
  atomic_fetch_add_explicit (&sched.epoch, 1u, memory_order_release);
  workers = sched.worker_count;
  queue_unlock ();
  crug_pal_wake_all (&sched.epoch);

  for (i = 0; i < workers; i++)
    {
      crug_task_join_hook (i, sched.threads[i]);
      if (crug_pal_thread_join (sched.threads[i]) != 0)
	{
	  /* The backend kept the handle: it did not join and did not free it.
	     The runtime is not stopped, so keep stopping/started/queue/live and
	     report failure.  Record it so a later caller reports the same
	     failure instead of pretending to succeed or waiting out the
	     overlap timeout.  */
	  atomic_store_explicit (&sched.stop_failed, 1u, memory_order_release);
	  return 3;
	}
      sched.threads[i] = NULL;
    }

  stack_local_flush ();
  stack_cache_flush ();
  task_cache_flush ();

  queue_lock ();
  sched.worker_count = 0;
  sched.head = NULL;
  sched.tail = NULL;
  sched.queue_length = 0;
  atomic_store_explicit (&sched.live, 0u, memory_order_release);
  atomic_store_explicit (&sched.epoch, 0u, memory_order_release);
  atomic_store_explicit (&sched.shutdown, 0u, memory_order_release);
  atomic_store_explicit (&sched.stopping, 0u, memory_order_release);
  atomic_store_explicit (&sched.stop_failed, 0u, memory_order_release);
  atomic_store_explicit (&sched.started, 0u, memory_order_release);
  atomic_store_explicit (&sched.start_state, 0, memory_order_release);
  queue_unlock ();
  atomic_store_explicit (&sched.lost,
                         atomic_load_explicit (&sched.spawned, memory_order_acquire)
                           - atomic_load_explicit (&sched.completed, memory_order_acquire),
                         memory_order_release);
  return 0;
}

void
crug_task_runtime_shutdown (void)
{
  int status = crug_task_runtime_try_shutdown ();

  if (status == 0 || status == 1)
    return;
  if (status == 2)
    crug_runtime_fatal ("a task cannot shut down the task runtime");
  crug_runtime_fatal ("cannot shut down the task runtime: a worker did not join");
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
  out->queue_peak = atomic_load_explicit (&sched.queue_peak,
					  memory_order_relaxed);
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
