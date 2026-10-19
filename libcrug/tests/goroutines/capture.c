
/* capture.c - the argument frame: no arguments and a struct by value.  An
   array decays to a pointer and is not Send (ADR 0026 D2).
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

static int ticks;
static int buf[4];
static int pair_sum;

struct Pair
{
  int a;
  int b;
};

static void
tick (void)
{
  ticks++;
}

static void
fill (void)
{
  for (int i = 0; i < 4; i++)
    buf[i] = i + 1;
}

static void
use_pair (struct Pair p)
{
  pair_sum = p.a + p.b;
}

int
main (void)
{
  struct Pair p = { 7, 9 };

  go tick ();
  go fill ();
  go use_pair (p);
  crug_task_runtime_shutdown ();

  printf ("goroutines-capture: ticks=%d buf=%d,%d,%d,%d pair=%d\n",
	  ticks, buf[0], buf[1], buf[2], buf[3], pair_sum);

  return (ticks == 1 && buf[0] == 1 && buf[3] == 4 && pair_sum == 16) ? 0 : 1;
}
