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
static int reread (ref struct Foo *r)
{
  ref struct Foo *q = borrow (r);
  return q->v;
}

int main (void)
{
  int r;
  {
    own struct Foo *p = adopt (my_alloc (sizeof *p), my_free);
    p->v = 6;
    r = reread (p);
  }
  if (destroyed != 1)
    return 2;
  return r == 6 ? 0 : 3;
}
