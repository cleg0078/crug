/* scheduler_fuzz.c - the Phase 22 randomized scheduler/scope/channel harness.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Each sequence builds a random operation list against the public task,
   scope and channel APIs (task.h, chan.h) and then asserts the runtime
   invariants after a shutdown: every spawned task completed exactly once,
   nothing was lost or corrupted, the yielded/scope/channel counters agree
   with what the sequence asked for, no task group is left live, and a second
   shutdown is a no-op.  The tables below are the oracle: they are built by
   the main thread while the operations are issued, never by the tasks.

   The harness is deterministic per (FUZZ_SEED, sequence): a failure prints
   the sequence number and seed, and FUZZ_SEED=<seed> re-runs it alone.
   FUZZ_RUNS selects the number of sequences (default 2000).  It is built
   with ASan and UBSan (fuzz/runtime/run.sh), so a broken switch, a leaked
   scope or a channel state error is a report and not a silent pass.
*/

#define _POSIX_C_SOURCE 200809L

#include "chan.h"
#include "task.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TASKS 512
#define MAX_CHANNELS 512

/* One frame per task; the runtime copies it at spawn.  */
struct frame
{
  uint32_t slot;              /* completion slot this task owns             */
  uint32_t yields;            /* explicit crug_task_yield() calls           */
  uint32_t mode;              /* MODE_* below                               */
  uint32_t children;          /* nested scope children (MODE_NESTED)        */
  uint32_t base_slot;         /* first completion slot of those children    */
  uint32_t pad;
  void *chan;                 /* channel of the operation, if any           */
  uint64_t expected;          /* value to send or to verify on receive      */
};

enum
{
  MODE_PLAIN = 0,
  MODE_RECV = 1,
  MODE_SEND = 2,
  MODE_NESTED = 3
};

static _Atomic uint32_t done_count[MAX_TASKS];
static _Atomic uint64_t task_failures;

static uint64_t expected_spawned;
static uint64_t expected_yields;
static uint64_t expected_scopes;
static uint64_t expected_sent;
static uint64_t expected_received;
static uint64_t expected_channels;
static uint32_t next_slot;

static uint64_t rng_state;
static uint64_t current_sequence;
static uint64_t current_seed;
static uint64_t failures;

static void
record_failure (const char *what)
{
  fprintf (stderr, "FAIL: scheduler-fuzz: sequence %llu seed 0x%llx: %s\n",
	   (unsigned long long) current_sequence,
	   (unsigned long long) current_seed, what);
  failures++;
}

#define CHECK(cond, what)                                                     \
  do                                                                          \
    {                                                                         \
      if (!(cond))                                                            \
	record_failure (what);                                                \
    }                                                                         \
  while (0)

static uint64_t
splitmix64 (uint64_t value)
{
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

static uint32_t
rnd_below (uint32_t bound)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return bound == 0 ? 0 : (uint32_t) (rng_state % bound);
}

static uint64_t
rnd64 (void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

/* The stack checksum a task verifies before it returns.  It depends only on
   the copied frame, so a task resumed on the wrong stack (or two tasks sharing
   one) changes it.  */
static uint64_t
scratch_sum (uint32_t slot, uint32_t yields)
{
  uint64_t sum = 0;
  uint32_t i;

  for (i = 0; i < 128u; i++)
    sum += (unsigned char) (slot * 31u + i * 7u + yields);
  return sum;
}

static void
task_entry (void *opaque)
{
  struct frame f = *(const struct frame *) opaque;
  unsigned char scratch[128];
  uint64_t sum = 0;
  uint32_t i;

  for (i = 0; i < sizeof scratch; i++)
    scratch[i] = (unsigned char) (f.slot * 31u + i * 7u + f.yields);
  if (crug_task_current_id () == 0u)
    atomic_fetch_add_explicit (&task_failures, 1u, memory_order_relaxed);

  if (f.mode == MODE_RECV)
    {
      const uint64_t *got = (const uint64_t *) crug_chan_recv_ptr (f.chan);
      if (got == NULL || *got != f.expected)
	atomic_fetch_add_explicit (&task_failures, 1u, memory_order_relaxed);
    }
  else if (f.mode == MODE_SEND)
    {
      uint64_t value = f.expected;
      crug_chan_send (f.chan, &value);
    }
  else if (f.mode == MODE_NESTED)
    {
      void *scope = crug_scope_enter ();
      for (i = 0; i < f.children; i++)
	{
	  struct frame child;
	  memset (&child, 0, sizeof child);
	  child.slot = f.base_slot + i;
	  child.yields = 1;
	  child.mode = MODE_PLAIN;
	  crug_task_spawn_scoped (scope, task_entry, &child, sizeof child);
	}
      crug_scope_leave (scope);
    }

  for (i = 0; i < f.yields; i++)
    crug_task_yield ();
  for (i = 0; i < sizeof scratch; i++)
    sum += scratch[i];
  if (sum != scratch_sum (f.slot, f.yields))
    atomic_fetch_add_explicit (&task_failures, 1u, memory_order_relaxed);

  atomic_fetch_add_explicit (&done_count[f.slot], 1u, memory_order_relaxed);
}

/* Reserve a completion slot and account for the task in the oracle.  */
static struct frame
make_frame (uint32_t mode, uint32_t yields, void *chan, uint64_t expected)
{
  struct frame f;

  memset (&f, 0, sizeof f);
  f.slot = next_slot++;
  f.yields = yields;
  f.mode = mode;
  f.chan = chan;
  f.expected = expected;
  expected_spawned++;
  expected_yields += yields;
  return f;
}

static void
spawn_frame (struct frame *frame)
{
  crug_task_spawn (task_entry, frame, sizeof *frame);
}

static void
spawn_scoped_frame (void *scope, struct frame *frame)
{
  crug_task_spawn_scoped (scope, task_entry, frame, sizeof *frame);
}

/* A buffered channel whose value the main thread pre-loaded, so the receiving
   task never blocks.  */
static void
op_buffered_receive (void)
{
  void *chan = crug_chan_make (1, sizeof (uint64_t));
  struct frame frame;
  uint64_t value = rnd64 ();

  expected_channels++;
  expected_sent++;
  expected_received++;
  crug_chan_send (chan, &value);
  frame = make_frame (MODE_RECV, rnd_below (3), chan, value);
  spawn_frame (&frame);
}

/* A task sends into a buffered channel; the main thread receives, which may
   sleep in the PAL but never yields a task.  */
static void
op_buffered_send (int *exact_yields)
{
  void *chan = crug_chan_make (1, sizeof (uint64_t));
  struct frame frame;
  uint64_t value = rnd64 ();
  uint64_t got = 0;
  const uint64_t *recv;

  expected_channels++;
  expected_sent++;
  expected_received++;
  frame = make_frame (MODE_SEND, 0, chan, value);
  spawn_frame (&frame);
  recv = (const uint64_t *) crug_chan_recv_ptr (chan);
  if (recv != NULL)
    got = *recv;
  CHECK (got == value, "buffered producer value");
  (void) exact_yields;
}

/* Capacity 0 is a handoff: the sending task deposits the value and then
   yields until the receiver takes it, so the task side of the channel wait is
   exercised.  The extra yields are not predictable, which is why the sequence
   stops asserting an exact yield count.  */
static void
op_rendezvous (int *exact_yields)
{
  void *chan = crug_chan_make (0, sizeof (uint64_t));
  struct frame frame;
  uint64_t value = rnd64 ();
  uint64_t got = 0;
  const uint64_t *recv;

  expected_channels++;
  expected_sent++;
  expected_received++;
  *exact_yields = 0;
  frame = make_frame (MODE_SEND, 0, chan, value);
  spawn_frame (&frame);
  recv = (const uint64_t *) crug_chan_recv_ptr (chan);
  if (recv != NULL)
    got = *recv;
  CHECK (got == value, "rendezvous value");
}

static void
op_plain (void)
{
  uint32_t count = 1 + rnd_below (6);
  uint32_t i;

  for (i = 0; i < count; i++)
    {
      struct frame frame = make_frame (MODE_PLAIN, rnd_below (4), NULL, 0);
      spawn_frame (&frame);
    }
}

static void
op_scope (void)
{
  void *scope = crug_scope_enter ();
  uint32_t count = 1 + rnd_below (5);
  uint32_t i;

  expected_scopes++;
  for (i = 0; i < count; i++)
    {
      struct frame frame = make_frame (MODE_PLAIN, rnd_below (4), NULL, 0);
      spawn_scoped_frame (scope, &frame);
    }
  crug_scope_leave (scope);
}

/* A task creates the group, spawns its children into it and leaves it, so the
   yielding wait of crug_scope_leave() inside a task is covered too.  */
static void
op_nested_scope (int *exact_yields)
{
  struct frame frame = make_frame (MODE_NESTED, rnd_below (3), NULL, 0);
  uint32_t children = 1 + rnd_below (3);

  *exact_yields = 0;
  expected_scopes++;
  frame.children = children;
  frame.base_slot = next_slot;
  next_slot += children;
  expected_spawned += children;
  expected_yields += children;   /* each child yields once */
  spawn_frame (&frame);
}

/* A scope whose children mix a pre-loaded receive with plain yielders.  */
static void
op_scope_mixed (void)
{
  void *scope = crug_scope_enter ();
  void *chan = crug_chan_make (1, sizeof (uint64_t));
  uint64_t value = rnd64 ();
  uint32_t count = 1 + rnd_below (3);
  uint32_t i;

  expected_scopes++;
  expected_channels++;
  expected_sent++;
  expected_received++;
  crug_chan_send (chan, &value);
  {
    struct frame frame = make_frame (MODE_RECV, 0, chan, value);
    spawn_scoped_frame (scope, &frame);
  }
  for (i = 0; i < count; i++)
    {
      struct frame frame = make_frame (MODE_PLAIN, rnd_below (3), NULL, 0);
      spawn_scoped_frame (scope, &frame);
    }
  crug_scope_leave (scope);
}

static int
same_task_stats (const struct crug_task_stats *a,
		 const struct crug_task_stats *b)
{
  return a->spawned == b->spawned && a->completed == b->completed
	 && a->yielded == b->yielded && a->lost == b->lost
	 && a->stack_corrupt == b->stack_corrupt && a->workers == b->workers
	 && a->queue_peak == b->queue_peak && a->live == b->live;
}

static void
check_sequence (const struct crug_task_stats *before,
		const struct crug_task_stats *first,
		const struct crug_task_stats *second,
		const struct crug_scope_stats *s_before,
		const struct crug_scope_stats *s_after,
		const struct crug_chan_stats *c_before,
		const struct crug_chan_stats *c_after,
		int exact_yields, int rendezvous)
{
  uint64_t spawned = second->spawned - before->spawned;
  uint64_t completed = second->completed - before->completed;
  uint64_t yielded = second->yielded - before->yielded;
  uint64_t entered = s_after->entered - s_before->entered;
  uint64_t left = s_after->left - s_before->left;
  uint64_t created = c_after->created - c_before->created;
  uint64_t sent = c_after->sent - c_before->sent;
  uint64_t received = c_after->received - c_before->received;
  uint32_t i;

  CHECK (spawned == expected_spawned, "spawned count disagrees with the oracle");
  CHECK (completed == expected_spawned, "completed count disagrees with the oracle");
  CHECK (second->live == 0, "tasks still live after shutdown");
  CHECK (second->lost == 0, "lost tasks after shutdown");
  CHECK (second->stack_corrupt == 0, "stack corruption reported");
  CHECK (second->workers == first->workers, "worker count changed by a no-op shutdown");
  CHECK (second->workers >= 1 && second->workers <= CRUG_TASK_WORKERS_MAX,
	 "worker count out of range");
  CHECK (second->queue_peak >= 1, "queue peak never advanced");
  CHECK (same_task_stats (first, second), "shutdown is not idempotent");
  CHECK (yielded >= expected_yields, "fewer yields than the oracle");
  if (exact_yields)
    CHECK (yielded == expected_yields, "yield count disagrees with the oracle");
  CHECK (entered == expected_scopes, "scope enter count disagrees with the oracle");
  CHECK (left == expected_scopes, "scope leave count disagrees with the oracle");
  CHECK (s_after->live == 0, "a task group is still live after shutdown");
  CHECK (created == expected_channels, "channel create count disagrees with the oracle");
  CHECK (sent == expected_sent, "channel send count disagrees with the oracle");
  CHECK (received == expected_received, "channel receive count disagrees with the oracle");
  CHECK (c_after->live == c_after->created, "a channel left the registry");
  if (rendezvous)
    CHECK (c_after->wait - c_before->wait >= 1, "rendezvous never waited");
  CHECK (atomic_load_explicit (&task_failures, memory_order_relaxed) == 0,
	 "a task observed a wrong value, id or stack checksum");
  for (i = 0; i < next_slot; i++)
    if (atomic_load_explicit (&done_count[i], memory_order_relaxed) != 1u)
      {
	CHECK (0, "a task did not complete exactly once");
	break;
      }
}

/* K-013/K-014 make a spawn that arrives after the stop claim fatal
   (crug-CONC-029, TASKS.md section 9), while a nested-scope task admits its
   children from inside the runtime.  An unadmitted spawn after the stop
   claim is therefore a harness bug, not a runtime bug: wait until every frame
   of this sequence has been admitted before asking for the stop, so the
   shutdown drain only runs tasks that cannot spawn any more.  */
static void
wait_for_admission (uint64_t want)
{
  struct crug_task_stats stats;
  unsigned polls;

  for (polls = 0; polls < 5000u; polls++)
    {
      struct timespec pause = { 0, 1000000L };
      int rc;

      crug_task_stats_get (&stats);
      if (stats.spawned >= want)
	return;
      rc = nanosleep (&pause, NULL);
      (void) rc;
    }
  CHECK (0, "not every task was admitted before the shutdown");
}

static void
run_sequence (uint64_t sequence, uint64_t base_seed,
	      uint64_t *total_tasks, uint64_t *total_yields,
	      uint64_t *total_scopes, uint64_t *total_channels)
{
  struct crug_task_stats before, first, second;
  struct crug_scope_stats s_before, s_after;
  struct crug_chan_stats c_before, c_after;
  uint32_t operations;
  uint32_t i;
  int exact_yields = 1;
  int rendezvous = 0;

  current_sequence = sequence;
  current_seed = splitmix64 (base_seed + sequence * 0x9e3779b97f4a7c15ull);
  rng_state = current_seed;

  for (i = 0; i < MAX_TASKS; i++)
    atomic_store_explicit (&done_count[i], 0u, memory_order_relaxed);
  next_slot = 0;
  expected_spawned = 0;
  expected_yields = 0;
  expected_scopes = 0;
  expected_sent = 0;
  expected_received = 0;
  expected_channels = 0;

  crug_task_stats_get (&before);
  crug_scope_stats_get (&s_before);
  crug_chan_stats_get (&c_before);

  operations = 1 + rnd_below (6);
  for (i = 0; i < operations; i++)
    {
      if (next_slot + 12u >= MAX_TASKS || expected_channels + 2u >= MAX_CHANNELS)
	break;
      switch (rnd_below (7))
	{
	case 0:
	  op_plain ();
	  break;
	case 1:
	  op_scope ();
	  break;
	case 2:
	  op_nested_scope (&exact_yields);
	  break;
	case 3:
	  op_buffered_receive ();
	  break;
	case 4:
	  op_buffered_send (&exact_yields);
	  break;
	case 5:
	  op_rendezvous (&exact_yields);
	  rendezvous = 1;
	  break;
	default:
	  op_scope_mixed ();
	  break;
	}
    }

  wait_for_admission (before.spawned + expected_spawned);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&first);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&second);
  crug_scope_stats_get (&s_after);
  crug_chan_stats_get (&c_after);

  check_sequence (&before, &first, &second, &s_before, &s_after,
		  &c_before, &c_after, exact_yields, rendezvous);

  *total_tasks += expected_spawned;
  *total_yields += expected_yields;
  *total_scopes += expected_scopes;
  *total_channels += expected_channels;
}

int
main (void)
{
  const char *runs_text = getenv ("FUZZ_RUNS");
  const char *seed_text = getenv ("FUZZ_SEED");
  uint64_t runs = 2000;
  uint64_t base_seed = 0xc0dec0dec0dec0deull;
  uint64_t total_tasks = 0;
  uint64_t total_yields = 0;
  uint64_t total_scopes = 0;
  uint64_t total_channels = 0;
  uint64_t i;

  if (runs_text != NULL && *runs_text != '\0')
    {
      char *end = NULL;
      unsigned long long value = strtoull (runs_text, &end, 10);
      if (end == runs_text || *end != '\0' || value == 0)
	{
	  fprintf (stderr, "scheduler-fuzz: FUZZ_RUNS must be a positive integer\n");
	  return 1;
	}
      runs = (uint64_t) value;
    }
  if (seed_text != NULL && *seed_text != '\0')
    {
      char *end = NULL;
      unsigned long long value = strtoull (seed_text, &end, 0);
      if (end == seed_text || *end != '\0')
	{
	  fprintf (stderr, "scheduler-fuzz: FUZZ_SEED must be an integer\n");
	  return 1;
	}
      base_seed = (uint64_t) value;
    }

  /* Outside a task yield is a no-op and the id is zero (task.h).  */
  crug_task_yield ();
  CHECK (crug_task_current_id () == 0u, "main thread reports a task id");

  for (i = 0; i < runs; i++)
    run_sequence (i, base_seed, &total_tasks, &total_yields,
		  &total_scopes, &total_channels);

  /* The runtime restarts after every shutdown; a last idempotent shutdown
     outside any sequence must not change the counters either.  */
  {
    struct crug_task_stats last, again;
    crug_task_runtime_shutdown ();
    crug_task_stats_get (&last);
    crug_task_runtime_shutdown ();
    crug_task_stats_get (&again);
    CHECK (same_task_stats (&last, &again), "final shutdown is not idempotent");
    CHECK (last.live == 0, "final shutdown left a live task");
  }
  CHECK (crug_task_current_id () == 0u, "main thread id changed");
  crug_task_yield ();

  printf ("scheduler-fuzz: sequences=%llu tasks=%llu yields=%llu "
	  "scopes=%llu channels=%llu failures=%llu\n",
	  (unsigned long long) runs, (unsigned long long) total_tasks,
	  (unsigned long long) total_yields, (unsigned long long) total_scopes,
	  (unsigned long long) total_channels, (unsigned long long) failures);
  return failures == 0 ? 0 : 1;
}
