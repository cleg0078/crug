/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* Leaving the loop through a goto keeps the value owned on that path, and the
   drop after the label consumes it exactly once.  */

extern void *malloc (__SIZE_TYPE__);
void kd (int *p) { (void) p; }

void do_while_goto_out (int n)
{
  own int *p = adopt ((int *) malloc (4), kd);
  do
    {
      if (n == 2)
	goto out;
    }
  while (--n);
out:
  drop (p);
}
