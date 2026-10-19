/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* The two paths adopt with different destructors: the drop at the end of
   the scope would have no single static answer.  */

struct S { int x; };
static char pool[8];
static void *my_alloc (void) { return pool; }
static void d1 (void *p) { (void) p; }
static void d2 (void *p) { (void) p; }

void
f (int c)
{
  own struct S *p;
  if (c) /* { dg-error "conflicting destructors" } */
    p = adopt (my_alloc (), d1);
  else
    p = adopt (my_alloc (), d2);
}
