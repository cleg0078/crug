/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-channels -fcrug-ownership -fcrug-strict" } */

/* drop () on an owned receive is an explicit release that leaves the binding
   moved: a second drop is a use of a moved value.  */

void
twice (chan own int *c)
{
  own int *v = recv (c);
  drop (v);
  drop (v);			/* { dg-error "crug-UAM-001" } */
}
