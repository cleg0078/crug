/* { dg-do run } */
/* { dg-options "-O2 -fcrug-ownership" } */

/* Destructors run once, on every path that leaves the scope.  The program
   counts its own allocations and destructions, so it needs no libc.  */

struct S { int x; };
static char pool[64];
static int allocs;
static int frees;

static void *
my_alloc (void)
{
  allocs++;
  return pool;
}

static void
my_free (void *p)
{
  if (p != pool)
    frees = -1000;
  frees++;
}

int
main (void)
{
  {
    own struct S *outer = adopt (my_alloc (), my_free);
    {
      own struct S *inner = adopt (my_alloc (), my_free);
      inner->x = 2;
    }
    if (frees != 1)
      return 1;
    outer->x = 1;
  }
  return allocs == 2 && frees == 2 ? 0 : 2;
}
