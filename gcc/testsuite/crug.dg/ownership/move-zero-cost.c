/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership" } */

/* ADR 0003 D4: a move is a compile-time fact.  It lowers to the operand
   itself, so the object code contains no helper call.  The function below
   is deliberately too small to need a call of its own: the gate counts
   'call' instructions in the disassembly.  */

struct Foo { int x; };
extern struct Foo *raw_source;
extern struct Foo **sink;

void transfer (void)
{
  own struct Foo *p = raw_source;
  *sink = move (p);
}
