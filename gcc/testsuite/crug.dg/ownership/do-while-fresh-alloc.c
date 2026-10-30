/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* A do-while body that allocates and destroys its own object each iteration
   stays clean: nothing crosses the back edge, so the joined entry state is
   the pre-loop state again.  */

extern void *malloc (__SIZE_TYPE__);
void kd (int *p) { (void) p; }

void do_while_fresh_alloc (int n)
{
  do
    {
      own int *q = adopt ((int *) malloc (4), kd);
      drop (q);
    }
  while (--n);
}
