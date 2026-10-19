/* 17-scope.c - a scope is a task group: the block waits for every child.

   Build: make -C libcrug PAL=linux, then pass the four runtime objects and
   -pthread; see the row in examples/README.md.  The program prints
   'seen=10 mut=42 frees=1' and 'groups entered=2 left=2 live=0'.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>

#include "task.h"

static int seen;
static int frees;

static void
add (int value)
{
  seen += value;
}

static void
bump (mut int *p)
{
  *p += 1;
}

static void
take (own char *p)
{
  frees += p[0] != 0;
  release (p);
}

int
main (void)
{
  own int *n = adopt ((int *) malloc (sizeof (int)), free);
  own char *buf = malloc (4);
  struct crug_scope_stats groups;
  int after_inner;
  int total;

  *n = 41;
  buf[0] = 'S';

  scope {
    for (int i = 0; i < 4; i++)
      go add (i + 1);

    scope {
      go bump (borrow_mut (n));
    }
    after_inner = *n;

    go take (move (buf));
  }

  total = seen;
  crug_task_runtime_shutdown ();
  crug_scope_stats_get (&groups);
  drop (n);

  printf ("seen=%d mut=%d frees=%d\n", total, after_inner, frees);
  printf ("groups entered=%llu left=%llu live=%llu\n",
	  (unsigned long long) groups.entered,
	  (unsigned long long) groups.left,
	  (unsigned long long) groups.live);

  return (total == 10 && after_inner == 42 && frees == 1
	  && groups.live == 0) ? 0 : 1;
}
