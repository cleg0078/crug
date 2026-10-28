/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-channels -fcrug-ownership -fcrug-strict" } */

/* A receive from a channel with an owning element binds a value the receiver
   owns even though the element type erased the destructor.  drop () is the
   receiver's explicit release (there is no destructor to call), move ()
   transfers the ownership on, and releasing on both paths of a conditional is
   balanced.  ADR 0026 D4; CHANNELS.md "own T *p = recv (ch)".  */

void
released (chan own int *c)
{
  own int *v = recv (c);
  drop (v);
}

void
relinquished (chan own int *c)
{
  own int *v = recv (c);
  release (v);
}

void
moved_on (chan own int *c)
{
  own int *v = recv (c);
  own int *w = move (v);
  drop (w);
}

void
either (chan own int *c, int flag)
{
  own int *v = recv (c);
  if (flag)
    drop (v);
  else
    drop (v);
}
