/* Linked-list traversal kernel: sum the values of N nodes.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Pointer chasing has nothing to vectorize; this kernel is the control that
   shows the alias fact does not make a non-vectorizable loop slower.  */

#include <stddef.h>

#include "../kernel.h"

#define REPS 8

#if BENCH_ARM == 2
long
bench_list (ref const struct bench_node *head)
#elif BENCH_ARM == 1
long
bench_list (const struct bench_node *restrict head)
#else
long
bench_list (const struct bench_node *head)
#endif
{
  long sum = 0;
  for (int r = 0; r < REPS; r++)
    for (const struct bench_node *p = head; p != NULL; p = p->next)
      sum += p->v;
  return sum;
}
