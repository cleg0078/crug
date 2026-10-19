/* Benchmark harness: portable timing and checksum reporting.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Every kernel program prints one line:

       <name> <min-nanoseconds> <checksum>

   The driver (bench/run.py) parses it; the checksum must be equal for every
   arm of a kernel, otherwise the arms do not compute the same thing.  */

#ifndef CRUG_BENCH_HARNESS_H
#define CRUG_BENCH_HARNESS_H

#include <stdio.h>
#include <time.h>

static long long
bench_now_ns (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (long long) ts.tv_sec * 1000000000LL + (long long) ts.tv_nsec;
}

/* Run CALL RUNS times (each run = three calls) and keep the shortest.  The
   shortest run is the least disturbed one; the median is not stable enough to
   compare two arms within five percent on a noisy machine.  */

#define BENCH_RUN(call, out_ns)						\
  do									\
    {									\
      long long bench_best_ = -1;					\
      for (int bench_r_ = 0; bench_r_ < 7; bench_r_++)		\
	{								\
	  long long bench_t0_ = bench_now_ns ();			\
	  for (int bench_i_ = 0; bench_i_ < 3; bench_i_++)		\
	    {								\
	      call;							\
	    }								\
	  long long bench_t1_ = bench_now_ns ();			\
	  if (bench_best_ < 0 || bench_t1_ - bench_t0_ < bench_best_)	\
	    bench_best_ = bench_t1_ - bench_t0_;			\
	}								\
      (out_ns) = bench_best_;						\
    }									\
  while (0)

static void
bench_report (const char *name, long long ns, long long checksum)
{
  printf ("%s %lld %lld\n", name, ns, checksum);
}

#endif /* CRUG_BENCH_HARNESS_H */
