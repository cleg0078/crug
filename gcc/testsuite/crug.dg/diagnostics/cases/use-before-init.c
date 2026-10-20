/* crug-DROP-007: the owned value may be used before it is initialized.  */
struct S { int x; };
static char pool[8];
static void *my_alloc (void) { return pool; }
static void my_free (void *p) { (void) p; }

void f (int c)
{
  own struct S *p;
  if (c)
    p = adopt (my_alloc (), my_free);
}
