/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */

void consume (mut int *p);

void
f (int c, own int *p)
{
  ref int *r;
  switch (c)
    {
    case 0:
      r = borrow (p);
      break;
    }
  /* The case may not be taken, so p is only conditionally borrowed.  */
  consume (borrow_mut (p)); /* { dg-error "crug-BORROW-002" } */
  (void) r;
}
