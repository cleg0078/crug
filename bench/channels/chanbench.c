/* chanbench.c - Phase 21 channel measurements, plain C against chan.h.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   One process per mode, so each mode starts from a clean runtime:

     chanbench pingpong    two tasks ping-pong one int over two unbuffered
                           channels; the best of CHAN_RUNS runs is reported
     chanbench throughput  one producer and one consumer move TP_ELEMENTS ints
                           over one buffered channel
     chanbench memory      make MEM_CHANNELS channels and divide the VmSize
                           delta by the channel count
     chanbench scaling     one, two and four independent producer/consumer
                           pairs; each pair moves SCALE_ELEMENTS ints

   Every mode prints one line (scaling prints one per pair count) of the form

     channels <mode> key=value ...

   Timing uses clock_gettime(CLOCK_MONOTONIC) through bench_now_ns().  Tasks
   start on a gate: the main thread spawns them, waits until each task reports
   it is running, records t0, opens the gate and waits for the tasks to
   report completion.  Startup and shutdown of the runtime are therefore
   outside the timed window, and the measured number is the channel work
   itself.  */

#define _GNU_SOURCE 1

#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chan.h"
#include "harness.h"
#include "task.h"

#define CHAN_RUNS 8
#define PP_ROUNDS 20000
#define TP_ELEMENTS 200000
#define TP_CAPACITY 1024
#define SCALE_ELEMENTS 100000
#define SCALE_CAPACITY 1024
#define MEM_CHANNELS 2048
#define MEM_CAPACITY 1024

static _Atomic int start_gate;
static _Atomic int started;
static _Atomic int finished;

struct meminfo
{
  unsigned long long vmsize_kb;
  unsigned long long vmrss_kb;
};

static int
read_mem (struct meminfo *out)
{
  FILE *f = fopen ("/proc/self/status", "r");
  char line[512];
  unsigned long long value;
  int have_size = 0;
  int have_rss = 0;

  if (f == NULL)
    return -1;
  out->vmsize_kb = 0;
  out->vmrss_kb = 0;
  while (fgets (line, sizeof line, f) != NULL)
    {
      if (sscanf (line, "VmSize: %llu kB", &value) == 1)
	{
	  out->vmsize_kb = value;
	  have_size = 1;
	}
      else if (sscanf (line, "VmRSS: %llu kB", &value) == 1)
	{
	  out->vmrss_kb = value;
	  have_rss = 1;
	}
    }
  fclose (f);
  return have_size && have_rss ? 0 : -1;
}

static void
begin_run (void)
{
  atomic_store_explicit (&start_gate, 0, memory_order_relaxed);
  atomic_store_explicit (&started, 0, memory_order_relaxed);
  atomic_store_explicit (&finished, 0, memory_order_relaxed);
}

static void
task_entry (void)
{
  atomic_fetch_add_explicit (&started, 1, memory_order_acq_rel);
  while (atomic_load_explicit (&start_gate, memory_order_acquire) == 0)
    crug_task_yield ();
}

static void
task_finish (void)
{
  atomic_fetch_add_explicit (&finished, 1, memory_order_acq_rel);
}

static void
wait_started (int count)
{
  while (atomic_load_explicit (&started, memory_order_acquire) < count)
    sched_yield ();
}

static void
wait_finished (int count)
{
  while (atomic_load_explicit (&finished, memory_order_acquire) < count)
    sched_yield ();
}

static void
open_gate (void)
{
  atomic_store_explicit (&start_gate, 1, memory_order_release);
}

static void
fail (const char *what)
{
  fprintf (stderr, "chanbench: %s\n", what);
  exit (2);
}

/* ---- ping-pong -------------------------------------------------------- */

struct pingpong
{
  void *to_consumer;
  void *to_producer;
  long long rounds;
  _Atomic long long sum;
  _Atomic long long bad;
};

static struct pingpong pingpong_state;

static void
pingpong_producer (void *frame)
{
  struct pingpong *p = *(struct pingpong **) frame;
  long long i;

  task_entry ();
  for (i = 0; i < p->rounds; i++)
    {
      int value = (int) i;
      int back;

      crug_chan_send (p->to_consumer, &value);
      back = *(int *) crug_chan_recv_ptr (p->to_producer);
      if (back != value)
	atomic_fetch_add_explicit (&p->bad, 1, memory_order_relaxed);
      atomic_fetch_add_explicit (&p->sum, back, memory_order_relaxed);
    }
  task_finish ();
}

static void
pingpong_consumer (void *frame)
{
  struct pingpong *p = *(struct pingpong **) frame;
  long long i;

  task_entry ();
  for (i = 0; i < p->rounds; i++)
    {
      int value = *(int *) crug_chan_recv_ptr (p->to_consumer);
      if (value != (int) i)
	atomic_fetch_add_explicit (&p->bad, 1, memory_order_relaxed);
      crug_chan_send (p->to_producer, &value);
    }
  task_finish ();
}

static long long
pingpong_run (long long rounds, long long *checksum)
{
  struct pingpong *p = &pingpong_state;
  void *frame = p;
  long long t0, t1;

  p->rounds = rounds;
  atomic_store_explicit (&p->sum, 0, memory_order_relaxed);
  atomic_store_explicit (&p->bad, 0, memory_order_relaxed);
  begin_run ();
  crug_task_spawn (pingpong_producer, &frame, sizeof frame);
  crug_task_spawn (pingpong_consumer, &frame, sizeof frame);
  wait_started (2);
  t0 = bench_now_ns ();
  open_gate ();
  wait_finished (2);
  t1 = bench_now_ns ();
  crug_task_runtime_shutdown ();

  *checksum = atomic_load_explicit (&p->sum, memory_order_relaxed);
  if (atomic_load_explicit (&p->bad, memory_order_relaxed) != 0)
    fail ("ping-pong values were reordered or lost");
  if (*checksum != rounds * (rounds - 1) / 2)
    fail ("ping-pong values did not cross the channels intact");
  return t1 - t0;
}

static void
mode_pingpong (void)
{
  long long best = -1;
  long long ns;
  long long checksum = 0;
  long long csum = 0;
  int run;

  /* The channels are made once; a channel is never freed in v1.  */
  pingpong_state.to_consumer = crug_chan_make (0, sizeof (int));
  pingpong_state.to_producer = crug_chan_make (0, sizeof (int));
  for (run = 0; run < CHAN_RUNS; run++)
    {
      ns = pingpong_run (PP_ROUNDS, &csum);
      if (best < 0 || ns < best)
	best = ns;
      if (run > 0 && csum != checksum)
	fail ("ping-pong checksum is not deterministic");
      checksum = csum;
    }
  printf ("channels pingpong best_ns=%lld rounds=%d checksum=%lld\n",
	  best, PP_ROUNDS, checksum);
}

/* ---- buffered throughput --------------------------------------------- */

struct throughput
{
  void *chan;
  long long elements;
  _Atomic long long sum;
  _Atomic long long bad;
};

static void
throughput_producer (void *frame)
{
  struct throughput *p = *(struct throughput **) frame;
  long long i;

  task_entry ();
  for (i = 0; i < p->elements; i++)
    {
      int value = (int) i;
      crug_chan_send (p->chan, &value);
    }
  task_finish ();
}

static void
throughput_consumer (void *frame)
{
  struct throughput *p = *(struct throughput **) frame;
  long long i;
  long long sum = 0;

  task_entry ();
  for (i = 0; i < p->elements; i++)
    {
      int value = *(int *) crug_chan_recv_ptr (p->chan);
      if (value != (int) i)
	atomic_fetch_add_explicit (&p->bad, 1, memory_order_relaxed);
      sum += value;
    }
  atomic_store_explicit (&p->sum, sum, memory_order_relaxed);
  task_finish ();
}

static long long
throughput_run (void *chan, long long elements, long long *checksum)
{
  static struct throughput state;
  struct throughput *p = &state;
  void *frame = p;
  long long t0, t1;

  p->chan = chan;
  p->elements = elements;
  atomic_store_explicit (&p->sum, 0, memory_order_relaxed);
  atomic_store_explicit (&p->bad, 0, memory_order_relaxed);
  begin_run ();
  crug_task_spawn (throughput_producer, &frame, sizeof frame);
  crug_task_spawn (throughput_consumer, &frame, sizeof frame);
  wait_started (2);
  t0 = bench_now_ns ();
  open_gate ();
  wait_finished (2);
  t1 = bench_now_ns ();
  crug_task_runtime_shutdown ();

  *checksum = atomic_load_explicit (&p->sum, memory_order_relaxed);
  if (atomic_load_explicit (&p->bad, memory_order_relaxed) != 0)
    fail ("throughput elements arrived out of order or were lost");
  if (*checksum != elements * (elements - 1) / 2)
    fail ("throughput elements were corrupted");
  return t1 - t0;
}

static void
mode_throughput (void)
{
  void *chan = crug_chan_make (TP_CAPACITY, sizeof (int));
  long long best = -1;
  long long ns;
  long long csum = 0;
  long long checksum = 0;
  int run;

  for (run = 0; run < CHAN_RUNS; run++)
    {
      ns = throughput_run (chan, TP_ELEMENTS, &csum);
      if (best < 0 || ns < best)
	best = ns;
      if (run > 0 && csum != checksum)
	fail ("throughput checksum is not deterministic");
      checksum = csum;
    }
  printf ("channels throughput best_ns=%lld elements=%d capacity=%d"
	  " checksum=%lld\n",
	  best, TP_ELEMENTS, TP_CAPACITY, checksum);
}

/* ---- memory per channel ---------------------------------------------- */

static void
mode_memory (void)
{
  struct meminfo before, after;
  unsigned long long ring = (unsigned long long) MEM_CAPACITY * sizeof (int);
  unsigned long long per_channel;
  unsigned long long rss_per_channel;
  long long overhead;
  int i;

  if (read_mem (&before) != 0)
    fail ("cannot read /proc/self/status");
  for (i = 0; i < MEM_CHANNELS; i++)
    {
      void *chan = crug_chan_make (MEM_CAPACITY, sizeof (int));
      if (crug_chan_cap (chan) != MEM_CAPACITY || crug_chan_len (chan) != 0)
	fail ("a made channel reports the wrong capacity");
    }
  if (read_mem (&after) != 0)
    fail ("cannot read /proc/self/status");
  if (after.vmsize_kb <= before.vmsize_kb)
    fail ("the channels did not grow the address space");

  per_channel = (after.vmsize_kb - before.vmsize_kb) * 1024u / MEM_CHANNELS;
  rss_per_channel = after.vmrss_kb > before.vmrss_kb
    ? (after.vmrss_kb - before.vmrss_kb) * 1024u / MEM_CHANNELS : 0;
  if (per_channel < ring)
    fail ("a channel uses less than its ring");
  overhead = (long long) per_channel - (long long) ring;

  printf ("channels memory per_channel_bytes=%llu ring_bytes=%llu"
	  " overhead_bytes=%lld rss_per_channel_bytes=%llu"
	  " channels=%d capacity=%d elem_size=%zu checksum=%d\n",
	  per_channel, ring, overhead, rss_per_channel,
	  MEM_CHANNELS, MEM_CAPACITY, sizeof (int), MEM_CHANNELS);
}

/* ---- scaling ---------------------------------------------------------- */

struct scale_pair
{
  void *chan;
  long long elements;
  _Atomic long long sum;
  _Atomic long long bad;
};

static struct scale_pair scale_pairs[4];

static void
scale_producer (void *frame)
{
  struct scale_pair *p = *(struct scale_pair **) frame;
  long long i;

  task_entry ();
  for (i = 0; i < p->elements; i++)
    {
      int value = (int) i;
      crug_chan_send (p->chan, &value);
    }
  task_finish ();
}

static void
scale_consumer (void *frame)
{
  struct scale_pair *p = *(struct scale_pair **) frame;
  long long i;
  long long sum = 0;

  task_entry ();
  for (i = 0; i < p->elements; i++)
    {
      int value = *(int *) crug_chan_recv_ptr (p->chan);
      if (value != (int) i)
	atomic_fetch_add_explicit (&p->bad, 1, memory_order_relaxed);
      sum += value;
    }
  atomic_store_explicit (&p->sum, sum, memory_order_relaxed);
  task_finish ();
}

static long long
scaling_run (int pairs, long long *checksum)
{
  void *frames[4];
  long long t0, t1;
  long long total = 0;
  long long bad = 0;
  int i;

  begin_run ();
  for (i = 0; i < pairs; i++)
    {
      scale_pairs[i].elements = SCALE_ELEMENTS;
      atomic_store_explicit (&scale_pairs[i].sum, 0, memory_order_relaxed);
      atomic_store_explicit (&scale_pairs[i].bad, 0, memory_order_relaxed);
      frames[i] = &scale_pairs[i];
    }
  for (i = 0; i < pairs; i++)
    {
      crug_task_spawn (scale_producer, &frames[i], sizeof frames[i]);
      crug_task_spawn (scale_consumer, &frames[i], sizeof frames[i]);
    }
  wait_started (pairs * 2);
  t0 = bench_now_ns ();
  open_gate ();
  wait_finished (pairs * 2);
  t1 = bench_now_ns ();
  crug_task_runtime_shutdown ();

  for (i = 0; i < pairs; i++)
    {
      total += atomic_load_explicit (&scale_pairs[i].sum, memory_order_relaxed);
      bad += atomic_load_explicit (&scale_pairs[i].bad, memory_order_relaxed);
    }
  *checksum = total;
  if (bad != 0)
    fail ("scaling elements arrived out of order or were lost");
  {
    long long per_pair = (long long) SCALE_ELEMENTS * (SCALE_ELEMENTS - 1) / 2;
    if (total != pairs * per_pair)
      fail ("scaling elements were corrupted");
  }
  return t1 - t0;
}

static void
mode_scaling (void)
{
  static const int counts[3] = { 1, 2, 4 };
  int c;

  for (c = 0; c < 3; c++)
    {
      int pairs = counts[c];
      long long best = -1;
      long long ns;
      long long csum = 0;
      long long checksum = 0;
      int run;
      int i;

      for (i = 0; i < pairs; i++)
	scale_pairs[i].chan = crug_chan_make (SCALE_CAPACITY, sizeof (int));
      for (run = 0; run < CHAN_RUNS; run++)
	{
	  ns = scaling_run (pairs, &csum);
	  if (best < 0 || ns < best)
	    best = ns;
	  if (run > 0 && csum != checksum)
	    fail ("scaling checksum is not deterministic");
	  checksum = csum;
	}
      printf ("channels scaling pairs=%d best_ns=%lld elements_per_pair=%d"
	      " capacity=%d checksum=%lld\n",
	      pairs, best, SCALE_ELEMENTS, SCALE_CAPACITY, checksum);
    }
}

static void
usage (void)
{
  fprintf (stderr,
	   "usage: chanbench pingpong|throughput|memory|scaling\n");
}

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      usage ();
      return 2;
    }
  if (strcmp (argv[1], "pingpong") == 0)
    mode_pingpong ();
  else if (strcmp (argv[1], "throughput") == 0)
    mode_throughput ();
  else if (strcmp (argv[1], "memory") == 0)
    mode_memory ();
  else if (strcmp (argv[1], "scaling") == 0)
    mode_scaling ();
  else
    {
      usage ();
      return 2;
    }
  return 0;
}
