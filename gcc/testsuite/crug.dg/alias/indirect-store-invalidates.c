/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

/* A write through an indirect pointer store re-points the alias, so the
   root recorded for 'r' at its declaration is stale.  Emitting a restrict
   fact from that stale root would promise what the call cannot prove.  */

void take (mut int *m, int *other);

void
f (own int *d)
{
  int other = 0;
  int *r = &other;
  int **pp = &r;
  *pp = d;
  take (borrow_mut (d), r); /* { dg-error "crug-BORROW-024" } */
}
