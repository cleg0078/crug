/* { dg-do run } */
/* { dg-options "-O2 -fcrug-strict" } */
struct Foo { int v; };

static int check (raw struct Foo *p)
{
  int r;
  unsafe { r = p->v; }
  return r;
}

int main (void)
{
  struct Foo f = { 7 };
  return check (&f) == 7 ? 0 : 1;
}
