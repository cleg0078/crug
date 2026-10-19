/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* The object owns a value on one path and nothing on the other, so a drop
   at the end of the scope would need a hidden flag (ADR 0003 D5).  */

struct S { int x; };
static char pool[8];
static void *my_alloc (void) { return pool; }
static void my_free (void *p) { (void) p; }

void
f (int c)
{
  own struct S *p; /* { dg-error "may be used before it is initialized" } */
  if (c)
    p = adopt (my_alloc (), my_free);
}
