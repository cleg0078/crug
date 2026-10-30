/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

/* A file-scope pointer to a fresh allocation is not a view of any lender: the
   allocation names a brand-new object, so it cannot alias the mut argument.  */

void add_into (mut int *d);

static int *g;

void
f (own int *d)
{
  g = __builtin_malloc (sizeof (int));
  add_into (borrow_mut (d));
}
