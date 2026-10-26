/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

void add_into (mut int *d, ref const int *a, int n);

void
f (own int *d)
{
  int *alias;
  __builtin_memcpy (&alias, (void *) &d, sizeof alias);
  /* The pass cannot see through the builtin copy.  */
  add_into (borrow_mut (d), alias, 1); /* { dg-error "crug-BORROW-024" } */
}
