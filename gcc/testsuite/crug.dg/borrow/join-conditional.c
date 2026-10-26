/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */

void consume (mut int *p);

void
f (int c, own int *p, own int *q)
{
  ref int *r;
  if (c)
    r = borrow (p);
  else
    r = borrow (q);
  /* p is shared-borrowed on the then path, so a mutable borrow of p here
     must see a live borrow even though liveness is path-sensitive.  */
  consume (borrow_mut (p)); /* { dg-error "crug-BORROW-002" } */
  (void) r;
}
