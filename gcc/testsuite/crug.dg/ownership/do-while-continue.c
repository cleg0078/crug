/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* A continue is a back edge to the condition; the joined entry state keeps
   an outer owned value owned when no iteration consumes it.  */

extern void *malloc (__SIZE_TYPE__);
void kd (int *p) { (void) p; }

void do_while_continue (int n)
{
  own int *p = adopt ((int *) malloc (4), kd);
  do
    {
      if (n < 0)
	continue;
      n--;
    }
  while (n > 0);
  drop (p);
}
