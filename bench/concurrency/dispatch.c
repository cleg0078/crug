/* dispatch.c - Phase 21 task-dispatch arms.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   One process per arm; the driver (bench/concurrency/run.py) runs each arm
   twice and keeps the smaller minimum:

     dispatch pthread     pthread_create()/pthread_join() per task
     dispatch pool        create a W-worker mutex+cond pool, submit N jobs,
                          wait until they ran, destroy the pool
     dispatch pool-warm   the same pool, created once and reused
     dispatch crug        crug_task_spawn() N times, then shutdown

   Every arm prints exactly one line

     dispatch <arm> ns=<min> checksum=<n>

   where <min> is the shortest of DISPATCH_RUNS complete dispatches of
   DISPATCH_TASKS tiny task bodies, and <checksum> is the number of task
   bodies that ran.  The checksum must be the same for every arm.

   The pool arm is timed over its whole lifecycle (create workers, submit,
   wait, join) because the Crug arm's dispatch is the whole lifecycle too:
   the first spawn starts the workers and shutdown joins them.  pool-warm is
   the same pool with its workers already running; it is reported for
   context and is not part of the pass/fail band.  */

#define _GNU_SOURCE 1

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "task.h"

#define DISPATCH_TASKS 200
#define DISPATCH_RUNS 8
#define POOL_WORKERS 8

static _Atomic long long work_done;
static long long arm_lost;

static void
do_work (void)
{
  atomic_fetch_add_explicit (&work_done, 1, memory_order_relaxed);
}

static void
reset_work (void)
{
  atomic_store_explicit (&work_done, 0, memory_order_relaxed);
}

static long long
work_count (void)
{
  return atomic_load_explicit (&work_done, memory_order_relaxed);
}

static void
check_work (const char *arm, long long expected)
{
  if (work_count () != expected)
    {
      fprintf (stderr, "dispatch: %s ran %lld task bodies, expected %lld\n",
	       arm, work_count (), expected);
      exit (2);
    }
}

/* ---- arm A: one pthread per task ------------------------------------- */

static void *
pthread_body (void *arg)
{
  (void) arg;
  do_work ();
  return NULL;
}

static long long
dispatch_pthread (void)
{
  pthread_t threads[DISPATCH_TASKS];
  long long t0, t1;
  int i;

  reset_work ();
  t0 = bench_now_ns ();
  for (i = 0; i < DISPATCH_TASKS; i++)
    if (pthread_create (&threads[i], NULL, pthread_body, NULL) != 0)
      {
	fprintf (stderr, "dispatch: pthread_create failed\n");
	exit (2);
      }
  for (i = 0; i < DISPATCH_TASKS; i++)
    pthread_join (threads[i], NULL);
  t1 = bench_now_ns ();
  check_work ("pthread", DISPATCH_TASKS);
  return t1 - t0;
}

/* ---- arm B: a fixed worker pool over a mutex + cond queue ------------- */

struct pool_job
{
  void (*fn) (void *);
  void *arg;
};

struct pool
{
  pthread_mutex_t lock;
  pthread_cond_t work_ready;
  pthread_cond_t work_done_cv;
  struct pool_job jobs[DISPATCH_TASKS];
  int head;
  int tail;
  int count;
  int done;
  int expected;
  int shutdown;
  pthread_t threads[POOL_WORKERS];
};

static void
pool_job_run (void *arg)
{
  (void) arg;
  do_work ();
}

static void *
pool_worker (void *arg)
{
  struct pool *p = (struct pool *) arg;

  pthread_mutex_lock (&p->lock);
  for (;;)
    {
      struct pool_job job;

      while (p->count == 0 && p->shutdown == 0)
	pthread_cond_wait (&p->work_ready, &p->lock);
      if (p->count == 0 && p->shutdown != 0)
	break;
      job = p->jobs[p->head];
      p->head = (p->head + 1) % DISPATCH_TASKS;
      p->count--;
      pthread_mutex_unlock (&p->lock);
      job.fn (job.arg);
      pthread_mutex_lock (&p->lock);
      p->done++;
      if (p->done == p->expected)
	pthread_cond_broadcast (&p->work_done_cv);
    }
  pthread_mutex_unlock (&p->lock);
  return NULL;
}

static struct pool *
pool_create (void)
{
  struct pool *p = (struct pool *) calloc (1, sizeof *p);
  int i;

  if (p == NULL)
    {
      fprintf (stderr, "dispatch: out of memory\n");
      exit (2);
    }
  p->expected = DISPATCH_TASKS;
  if (pthread_mutex_init (&p->lock, NULL) != 0
      || pthread_cond_init (&p->work_ready, NULL) != 0
      || pthread_cond_init (&p->work_done_cv, NULL) != 0)
    {
      fprintf (stderr, "dispatch: cannot initialise the pool\n");
      exit (2);
    }
  for (i = 0; i < POOL_WORKERS; i++)
    if (pthread_create (&p->threads[i], NULL, pool_worker, p) != 0)
      {
	fprintf (stderr, "dispatch: cannot create a pool worker\n");
	exit (2);
      }
  return p;
}

static void
pool_submit (struct pool *p)
{
  int i;

  pthread_mutex_lock (&p->lock);
  for (i = 0; i < DISPATCH_TASKS; i++)
    {
      p->jobs[p->tail].fn = pool_job_run;
      p->jobs[p->tail].arg = NULL;
      p->tail = (p->tail + 1) % DISPATCH_TASKS;
      p->count++;
    }
  pthread_cond_broadcast (&p->work_ready);
  pthread_mutex_unlock (&p->lock);
}

static void
pool_wait (struct pool *p)
{
  pthread_mutex_lock (&p->lock);
  while (p->done < p->expected)
    pthread_cond_wait (&p->work_done_cv, &p->lock);
  pthread_mutex_unlock (&p->lock);
}

static void
pool_destroy (struct pool *p)
{
  int i;

  pthread_mutex_lock (&p->lock);
  p->shutdown = 1;
  pthread_cond_broadcast (&p->work_ready);
  pthread_mutex_unlock (&p->lock);
  for (i = 0; i < POOL_WORKERS; i++)
    pthread_join (p->threads[i], NULL);
  pthread_mutex_destroy (&p->lock);
  pthread_cond_destroy (&p->work_ready);
  pthread_cond_destroy (&p->work_done_cv);
  free (p);
}

static long long
dispatch_pool_cold (void)
{
  struct pool *p;
  long long t0, t1;

  reset_work ();
  t0 = bench_now_ns ();
  p = pool_create ();
  pool_submit (p);
  pool_wait (p);
  pool_destroy (p);
  t1 = bench_now_ns ();
  check_work ("pool", DISPATCH_TASKS);
  return t1 - t0;
}

static long long
dispatch_pool_warm (struct pool *p)
{
  long long t0, t1;

  reset_work ();
  pthread_mutex_lock (&p->lock);
  p->done = 0;
  pthread_mutex_unlock (&p->lock);
  t0 = bench_now_ns ();
  pool_submit (p);
  pool_wait (p);
  t1 = bench_now_ns ();
  check_work ("pool-warm", DISPATCH_TASKS);
  return t1 - t0;
}

/* ---- arm C: the Crug runtime ----------------------------------------- */

static void
crug_body (void *frame)
{
  (void) frame;
  do_work ();
}

static long long
dispatch_crug (void)
{
  struct crug_task_stats before, after;
  long long t0, t1;
  int i;

  crug_task_stats_get (&before);
  reset_work ();
  t0 = bench_now_ns ();
  for (i = 0; i < DISPATCH_TASKS; i++)
    crug_task_spawn (crug_body, NULL, 0);
  crug_task_runtime_shutdown ();
  t1 = bench_now_ns ();
  crug_task_stats_get (&after);

  arm_lost = (long long) after.lost;
  if (after.spawned - before.spawned != DISPATCH_TASKS
      || after.completed - before.completed != DISPATCH_TASKS
      || after.lost != 0
      || after.stack_corrupt != 0)
    {
      fprintf (stderr,
	       "dispatch: crug stats spawned=%llu completed=%llu lost=%llu"
	       " corrupt=%llu\n",
	       (unsigned long long) (after.spawned - before.spawned),
	       (unsigned long long) (after.completed - before.completed),
	       (unsigned long long) after.lost,
	       (unsigned long long) after.stack_corrupt);
      exit (2);
    }
  check_work ("crug", DISPATCH_TASKS);
  return t1 - t0;
}

static void
usage (void)
{
  fprintf (stderr,
	   "usage: dispatch pthread|pool|pool-warm|crug\n");
}

int
main (int argc, char **argv)
{
  long long best = -1;
  long long ns;
  int run;

  if (argc != 2)
    {
      usage ();
      return 2;
    }

  if (strcmp (argv[1], "pool-warm") == 0)
    {
      struct pool *p = pool_create ();
      for (run = 0; run < DISPATCH_RUNS; run++)
	{
	  ns = dispatch_pool_warm (p);
	  if (best < 0 || ns < best)
	    best = ns;
	}
      pool_destroy (p);
    }
  else
    for (run = 0; run < DISPATCH_RUNS; run++)
      {
	if (strcmp (argv[1], "pthread") == 0)
	  ns = dispatch_pthread ();
	else if (strcmp (argv[1], "pool") == 0)
	  ns = dispatch_pool_cold ();
	else if (strcmp (argv[1], "crug") == 0)
	  ns = dispatch_crug ();
	else
	  {
	    usage ();
	    return 2;
	  }
	if (best < 0 || ns < best)
	  best = ns;
      }

  printf ("dispatch %s ns=%lld checksum=%lld lost=%lld\n",
	  argv[1], best, work_count (), arm_lost);
  return 0;
}
