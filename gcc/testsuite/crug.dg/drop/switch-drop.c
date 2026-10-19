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
  int i;
  for (i = 0; i < 3; i++)
    switch (i)
      {
      case 0:
	{
	  own struct S *p = adopt (my_alloc (), my_free);
	  p->x = 0;
	  break;
	}
      case 1:
	{
	  own struct S *p = adopt (my_alloc (), my_free);
	  p->x = 1;
	  break;
	}
      default:
	break;
      }
  return allocs == 2 && frees == 2 ? 0 : 1;
}
