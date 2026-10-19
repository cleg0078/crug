/* 19-send-sync.c - what may cross a task or channel boundary.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>

/* A struct of plain values is Send, so it may be copied into a task.  */
struct pair
{
  int a;
  int b;
};

static void
emit (chan int out, struct pair p)
{
  send (out, p.a + p.b);
}

/* A mut parameter is an exclusive scoped borrow: Send and exclusive.  */
static void
bump (mut int *n)
{
  *n = *n + 1;
}

/* An own parameter takes ownership, so the buffer crosses by move().  */
static void
take (own char *p)
{
  release (p);
}

int
main (void)
{
  chan int sums = channel (4);
  struct pair one = { 1, 2 };
  struct pair two = { 3, 4 };
  own int *count = adopt (malloc (sizeof (int)), free);
  *count = 0;

  scope {
    go emit (sums, one);
    go emit (sums, two);
    go bump (borrow_mut (count));
    own char *text = adopt (malloc (4), free);
    go take (move (text));
  }

  int total = recv (sums) + recv (sums);
  printf ("sum=%d count=%d\n", total, *count);
  release (count);

  /* Rejected by the same rules: go use (&x) and chan int * are not Send
     (CONC-024 / CONC-027), and a thread-affinity FFI handle cannot leave
     its thread (CONC-025).  Anything containing a raw pointer is refused
     until a .crugffi 'send sync' annotation says the author vouches for it.  */
  return 0;
}
