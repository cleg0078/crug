/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */

/* The borrow in the then arm is absent on the else path.  At the join its
   lender set must stay 'p' (and be marked possibly absent); dropping or
   re-attributing it made the later mutable borrow of the unrelated 'q'
   look like a conflict.  */

void take (mut int *m);

void
f (int c, own int *p, own int *q)
{
  ref int *r;

  if (c)
    r = borrow (p);
  take (borrow_mut (q));
  (void) r;
}
