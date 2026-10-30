/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* fix-p3 OWN-02: the second iteration of a do-while assigns over the owned
   value the first iteration stored, so the destructor the loop needs is
   reported and the overwrite leak is rejected.

   The joined entry state also makes the value MAYBE_EMPTY after the loop: the
   assignment's error recovery leaves it EMPTY on the analysed path while the
   probed back edge leaves it OWNED, and the conservative exit join of a loop
   reports that as a second diagnostic.  It is a cascade of the same error, not
   an independent finding.  */

extern void *malloc (__SIZE_TYPE__);
void kd (int *p) { (void) p; }

void do_while_leak (int n)
{
  own int *p = adopt ((int *) malloc (4), kd); /* { dg-error "crug-DROP-007" } */
  do
    p = adopt ((int *) malloc (4), kd); /* { dg-error "already owns" } */
  while (--n);
  drop (p);
}
