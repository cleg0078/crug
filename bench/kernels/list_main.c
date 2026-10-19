/* Linked-list driver.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>

#include "../harness.h"
#include "../kernel.h"

#define N 65536

static void
free_list (struct bench_node *head)
{
  while (head != NULL)
    {
      struct bench_node *next = head->next;
      free (head);
      head = next;
    }
}

int
main (void)
{
  struct bench_node *head = malloc (sizeof *head);
  head->v = 1;
  head->next = NULL;
  struct bench_node *tail = head;
  for (int i = 1; i < N; i++)
    {
      struct bench_node *n = malloc (sizeof *n);
      n->v = i;
      n->next = NULL;
      tail->next = n;
      tail = n;
    }

  long long ns;
  long sum;
  BENCH_RUN (sum = bench_list (head), ns);

  bench_report ("list", ns, sum);
  free_list (head);
  return 0;
}
