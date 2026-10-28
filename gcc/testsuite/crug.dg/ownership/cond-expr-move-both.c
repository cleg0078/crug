/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0003 D5: the two arms of a conditional are alternatives.  Moving the
   same object in both arms is therefore a single move on every path, not a
   second move of an already-moved value.  */

struct Foo { int x; };

extern struct Foo *raw_source;
extern int cond;

void f (void)
{
  own struct Foo *p = raw_source;
  cond ? (move (p), 1) : (move (p), 2);
}
