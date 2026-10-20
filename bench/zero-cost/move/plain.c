/* Plain-C twin of move/crug.c.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

struct node
{
  struct node *next;
  int v;
};

int
move_value (struct node *p)
{
  struct node *q = p;
  return q->v;
}
