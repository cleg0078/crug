/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0003 D5: every arm moves the same object out, so the object is moved on
   every path and no arm may be diagnosed as a second move of it.  */

struct Foo { int x; };

extern struct Foo *raw_source;
extern struct Foo **sink;
extern int cond;

void f (void)
{
  own struct Foo *p = raw_source;

  switch (cond)
    {
    case 0:
      *sink = move (p);
      break;
    default:
      *sink = move (p);
      break;
    }
}
