/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

/* OWN-03: a copy of the lender written to a file-scope object before the call
   is a second view of the object.  The callee's mut parameter is compiled as
   restrict, so the call must be rejected even though it passes only the
   borrow.  */

void add_into (mut int *d);

static int *g;

void
f (own int *d)
{
  g = d;
  add_into (borrow_mut (d)); /* { dg-error "crug-BORROW-024" } */
}
