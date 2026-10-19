/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* A goto that jumps into the scope of an owning object skips its
   initialization, so the object could be destroyed uninitialized.  */

struct S { int x; };
extern struct S *raw_source;

void
f (int c)
{
  if (c)
    goto inside; /* { dg-error "goto jumps into the scope" } */
  {
    own struct S *p = raw_source;
inside:
    p->x = 1;
  }
}
