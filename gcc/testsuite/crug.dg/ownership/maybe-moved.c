/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0003 D2: the join of OWNED and MOVED is MAYBE_MOVED, and using a
   possibly-moved value is an error rather than a silent assumption.  */

struct Foo { int x; };
extern struct Foo *raw_source;
extern struct Foo **sink;
extern int cond;

void maybe_moved (void)
{
  own struct Foo *p = raw_source;
  if (cond)
    move (p);
  *sink = p; /* { dg-error "use of possibly-moved value" } */
}
