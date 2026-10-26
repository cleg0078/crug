/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */

void consume (mut int *p);

void
f (int c, own int *p, own int *q)
{
  ref int *r;
  switch (c)
    {
    case 0:
      r = borrow (p);
      break;
    default:
      r = borrow (q);
      break;
    }
  consume (borrow_mut (p)); /* { dg-error "crug-BORROW-002" } */
  ref int *s = borrow (r); /* { dg-error "crug-BORROW-024" } */
  (void) s;
}
