/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* fix-p3 OWN-01: a do-while body runs again from the state its previous
   iteration leaves, so a move that every iteration performs is diagnosed
   instead of being treated as a single move (the audited hole was a double
   free at run time).  */

extern void *malloc (__SIZE_TYPE__);
void kd (int *p) { (void) p; }

void do_while_double_move (int n)
{
  own int *p = adopt ((int *) malloc (4), kd); /* { dg-error "crug-DROP-006" } */
  do
    {
      own int *b = move (p); /* { dg-error "move of possibly-moved" } */
      (void) b;
    }
  while (--n);
}
