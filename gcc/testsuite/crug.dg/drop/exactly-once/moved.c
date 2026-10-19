/* { dg-do run } */
/* { dg-options "-O2 -fcrug-ownership" } */

/* Exactly-once harness: no libc, the program counts its own allocations
   and destructions.  Exit status 0 means the count is right.  The owning
   variable lives in an inner block so its destruction happens before the
   count is checked.  */

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
    own struct S *p = adopt (my_alloc (), my_free);
    own struct S *q = move (p);
    q->x = 1;
  }
  return allocs == 1 && frees == 1 ? 0 : 1;
}
