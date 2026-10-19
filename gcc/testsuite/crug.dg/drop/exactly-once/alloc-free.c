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

/* The allocation catalogue gives malloc() the free() destructor.  The
   program defines its own malloc and free, which override libc at link
   time and count the calls.  */

void *
malloc (unsigned long n)
{
  (void) n;
  allocs++;
  return pool;
}

void
free (void *p)
{
  if (p != pool)
    frees = -1000;
  frees++;
}

int
main (void)
{
  {
    own struct S *p = malloc (sizeof *p);
    p->x = 1;
  }
  return allocs == 1 && frees == 1 ? 0 : 1;
}
