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
static int read_it (ref struct Foo *r)
{
  return r->v;
}

static void write_it (mut struct Foo *m)
{
  m->v = 11;
}

int main (void)
{
  int r;
  {
    own struct Foo *p = adopt (my_alloc (sizeof *p), my_free);
    p->v = 3;
    {
      ref struct Foo *a = borrow (p);
      r = read_it (a);
    }
    {
      mut struct Foo *m = borrow_mut (p);
      write_it (m);
    }
    if (p->v != 11)
      return 4;
  }
  if (destroyed != 1)
    return 2;
  return r == 3 ? 0 : 3;
}
