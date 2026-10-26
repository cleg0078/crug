/* chan_claim.c - the non-blocking claim pair behind 'select'.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The claims report 1 (completed), 0 (a blocking call would wait) and -1
   (closed), and they never touch the shared receive buffer.  The concurrent
   cases use tasks so the test proves the claims really complete a handoff
   with a waiting receiver and that several claim receivers share one channel
   without losing or duplicating a value.  */

#define _DEFAULT_SOURCE 1
#include "chan.h"
#include "task.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static void *handoff;
static _Atomic int claim_receiver_ok;
static _Atomic int claim_receiver_value;
static _Atomic int claim_sender_ok;

static void *shared;
static _Atomic int claimed;
static _Atomic int claimed_sum;
static _Atomic int producer_ok;
static _Atomic int consumer_a;
static _Atomic int consumer_b;

static void
set_one_worker (void)
{
#ifdef _WIN32
  _putenv_s ("CRUG_TASKS_WORKERS", "1");
#else
  setenv ("CRUG_TASKS_WORKERS", "1", 1);
#endif
}

static int
expect (int condition, const char *what)
{
  if (!condition)
    fprintf (stderr, "FAIL: chan-claim: %s\n", what);
  return condition ? 1 : 0;
}

/* ---- one blocking receiver and one claim sender on a cap-0 channel ---- */

static void
claim_receiver (void *frame)
{
  int value;
  (void) frame;
  value = *(int *) crug_chan_recv_ptr (handoff);
  atomic_store_explicit (&claim_receiver_value, value, memory_order_release);
  atomic_store_explicit (&claim_receiver_ok, value == 7, memory_order_release);
}

static void
claim_sender (void *frame)
{
  int value = 7;
  int polls;

  (void) frame;
  for (polls = 0; polls < 1000000; polls++)
    {
      int rc = crug_chan_send_claim (handoff, &value, sizeof value);
      if (rc == 1)
	{
	  atomic_store_explicit (&claim_sender_ok, 1, memory_order_release);
	  return;
	}
      if (rc < 0)
	return;
      crug_task_yield ();
    }
}

/* ---- two claim consumers sharing a buffered channel ------------------- */

static void
producer (void *frame)
{
  int i;
  (void) frame;
  for (i = 0; i < 100; i++)
    crug_chan_send (shared, &i);
  crug_chan_close (shared);
  atomic_store_explicit (&producer_ok, 1, memory_order_release);
}

static void
consumer (void *frame)
{
  int index = *(int *) frame;
  int count = 0;
  int value;

  for (;;)
    {
      int rc = crug_chan_recv_claim (shared, &value, sizeof value);
      if (rc == 1)
	{
	  count++;
	  atomic_fetch_add_explicit (&claimed, 1, memory_order_relaxed);
	  atomic_fetch_add_explicit (&claimed_sum, value, memory_order_relaxed);
	}
      else if (rc == -1)
	break;
      else
	crug_task_yield ();
    }
  if (index == 0)
    atomic_store_explicit (&consumer_a, count, memory_order_release);
  else
    atomic_store_explicit (&consumer_b, count, memory_order_release);
}

int
main (void)
{
  struct crug_task_stats stats;
  void *buffered;
  void *owned;
  void *zero;
  int value = 5;
  int other = 6;
  int out = -1;
  int ok = 1;
  int index_a = 0;
  int index_b = 1;

  /* ---- the report contract on a buffered channel, no tasks yet ---- */
  buffered = crug_chan_make (1, sizeof (int));
  ok &= expect (crug_chan_recv_claim (buffered, &out, sizeof out) == 0,
		"an empty buffered receive claim did not report 0");
  ok &= expect (out == -1, "a losing receive claim wrote to the buffer");
  ok &= expect (crug_chan_send_claim (buffered, &value, sizeof value) == 1,
		"a buffered send claim did not report 1");
  ok &= expect (crug_chan_send_claim (buffered, &other, sizeof other) == 0,
		"a full buffered send claim did not report 0");
  ok &= expect (crug_chan_recv_claim (buffered, &out, sizeof out) == 1
		&& out == value,
		"a buffered receive claim did not return the sent value");
  crug_chan_close (buffered);
  out = -1;
  ok &= expect (crug_chan_recv_claim (buffered, &out, sizeof out) == -1,
		"a closed receive claim did not report -1");
  ok &= expect (out == 0, "a closed receive claim did not zero the buffer");
  ok &= expect (crug_chan_send_claim (buffered, &value, sizeof value) == -1,
		"a closed send claim did not report -1");

  owned = crug_chan_make (1, sizeof (int));
  ok &= expect (crug_chan_send_owned_claim (owned, &value, sizeof value) == 1,
		"the owned claim spelling did not share the send claim code");
  out = -1;
  ok &= expect (crug_chan_recv_claim (owned, &out, sizeof out) == 1
		&& out == value,
		"the owned claim spelling did not deposit the value");

  zero = crug_chan_make (0, sizeof (int));
  ok &= expect (crug_chan_recv_claim (zero, &out, sizeof out) == 0,
		"an empty cap-0 receive claim did not report 0");
  ok &= expect (crug_chan_send_claim (zero, &value, sizeof value) == 0,
		"a cap-0 send claim without a waiting receiver did not report 0");

  /* ---- a claim send completes a handoff with a blocking receiver ---- */
  set_one_worker ();
  handoff = crug_chan_make (0, sizeof (int));
  crug_task_spawn (claim_receiver, NULL, 0);
  crug_task_spawn (claim_sender, NULL, 0);
  crug_task_runtime_shutdown ();

  ok &= expect (atomic_load_explicit (&claim_receiver_ok, memory_order_acquire),
		"the blocking receiver did not get the claim sender's value");
  ok &= expect (atomic_load_explicit (&claim_receiver_value,
				      memory_order_acquire) == 7,
		"the blocking receiver saw the wrong value");
  ok &= expect (atomic_load_explicit (&claim_sender_ok, memory_order_acquire) == 1,
		"the claim sender did not report a completed handoff");

  /* ---- competing claim consumers on one buffered channel ---- */
  shared = crug_chan_make (4, sizeof (int));
  crug_task_spawn (producer, NULL, 0);
  crug_task_spawn (consumer, &index_a, sizeof index_a);
  crug_task_spawn (consumer, &index_b, sizeof index_b);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);

  ok &= expect (atomic_load_explicit (&producer_ok, memory_order_acquire) == 1,
		"the producer did not finish");
  ok &= expect (atomic_load_explicit (&claimed, memory_order_acquire) == 100,
		"the consumers did not claim all 100 values");
  ok &= expect (atomic_load_explicit (&claimed_sum, memory_order_acquire) == 4950,
		"the consumers lost or duplicated a value");
  ok &= expect (atomic_load_explicit (&consumer_a, memory_order_acquire)
		+ atomic_load_explicit (&consumer_b, memory_order_acquire) == 100,
		"the per-consumer counts do not add up");
  ok &= expect (stats.completed == 5 && stats.lost == 0,
		"a task did not complete");

  printf ("chan-claim: completed=%llu claimed=%d sum=%d consumers=%d,%d\n",
	  (unsigned long long) stats.completed,
	  atomic_load_explicit (&claimed, memory_order_acquire),
	  atomic_load_explicit (&claimed_sum, memory_order_acquire),
	  atomic_load_explicit (&consumer_a, memory_order_acquire),
	  atomic_load_explicit (&consumer_b, memory_order_acquire));
  return ok ? 0 : 1;
}
