/* { dg-do run } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
static char pool[4096];
static unsigned long used;
static int destroyed;

static void *my_alloc (unsigned long n)
{
  void *p = pool + used;
  used += (n + 15) & ~15UL;
  return p;
}

static void my_free (void *p)
{
  (void) p;
  destroyed++;
}

struct Foo { int v; };
static int read_then_return (own struct Foo *p)
{
  ref struct Foo *a = borrow (p);
  if (a->v < 0)
    {
      /* The borrow is dead on this path, but it is used below on the other
	 path, so the owner cannot be moved before the join.  */
      return 1;
    }
  return a->v;
}

int main (void)
{
  int r;
  {
    own struct Foo *p = adopt (my_alloc (sizeof *p), my_free);
    p->v = 8;
    r = read_then_return (p);
    own struct Foo *q = move (p);
    r += q->v;
  }
  if (destroyed != 1)
    return 2;
  return r == 16 ? 0 : 3;
}
