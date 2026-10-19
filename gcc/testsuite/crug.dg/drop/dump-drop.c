/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -fcrug-dump=drop" } */

/* The gate greps the dump for the four exit kinds.  */

struct S { int x; };
static char pool[8];
static void *my_alloc (void) { return pool; }
static void my_free (void *p) { (void) p; }

void
scope_end (void)
{
  own struct S *p = adopt (my_alloc (), my_free);
  (void) p;
}

int
ret (int c)
{
  own struct S *p = adopt (my_alloc (), my_free);
  (void) p;
  if (c)
    return 1;
  return 0;
}
