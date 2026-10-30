/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

/* A member of a file-scope object is storage the pass does not track as a
   pointer variable; storing the lender there creates another view.  */

void add_into (mut int *d);

struct S { int *p; };

static struct S gs;

void
f (own int *d)
{
  gs.p = d;
  add_into (borrow_mut (d)); /* { dg-error "crug-BORROW-024" } */
}
