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
int main (void)
{
  int r;
  {
    own struct Foo *p = adopt (my_alloc (sizeof *p), my_free);
    p->v = 3;
    ref struct Foo *a = borrow (p);
    r = a->v;
    /* The borrow is dead, so the owner may be destroyed early.  */
    drop (p);
    r += destroyed;
  }
  if (destroyed != 1)
    return 2;
  return r == 4 ? 0 : 3;
}
