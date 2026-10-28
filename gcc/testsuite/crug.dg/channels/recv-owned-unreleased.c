/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-channels -fcrug-ownership -fcrug-strict" } */

/* An owned receive is the receiver's value: it must be dropped, released or
   moved on.  The binding has no known destructor, but leaving it untouched is
   still crug-OWN-007 (warn_unadopted at scope end).  */

void
forgot (chan own int *c)
{
  own int *v = recv (c);	/* { dg-error "crug-OWN-007" } */
  (void) v;
}
