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

static int
f (int c)
{
  own struct S *p = adopt (my_alloc (), my_free);
  if (c)
    return 1;
  else
    return 2;
}

int
main (void)
{
  if (f (1) != 1 || frees != 1)
    return 1;
  if (f (0) != 2 || frees != 2)
    return 2;
  return 0;
}
