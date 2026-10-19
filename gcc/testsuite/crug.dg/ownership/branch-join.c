/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -fcrug-dump=ownership" } */

/* ADR 0003 D1: a branch join that mixes OWNED and MOVED has no static
   answer, so the join is MAYBE_MOVED.  The gate greps the dump for
   "join ...: p=OWNED/MOVED -> MAYBE_MOVED".  */

struct Foo { int x; };
extern struct Foo *raw_source;
extern struct Foo **sink;
extern int cond;

void join (void)
{
  own struct Foo *p = raw_source;
  if (cond)
    *sink = move (p);
  else
    *sink = (struct Foo *) 0;
}
