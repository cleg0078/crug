/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* fix-p3 OWN-02b: reading or releasing a value that a previous iteration
   already released is a use after release, not a single well-ordered
   consumption.  */

extern void *malloc (__SIZE_TYPE__);
void kd (int *p) { (void) p; }

void do_while_use_after_drop (int n)
{
  own int *p = adopt ((int *) malloc (4), kd); /* { dg-error "crug-DROP-006" } */
  do
    {
      int v = *p; /* { dg-error "use of possibly-moved" } */
      (void) v;
      release (p); /* { dg-error "use of possibly-moved" } */
    }
  while (--n);
}
