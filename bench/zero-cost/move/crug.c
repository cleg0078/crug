/* Zero-cost case: move () of an owned parameter.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

struct node
{
  struct node *next;
  int v;
};

int
move_value (own struct node *p)
{
  struct node *q = move (p);
  return q->v;
}
