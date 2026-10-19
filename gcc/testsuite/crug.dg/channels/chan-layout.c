/* chan-layout.c - every channel type is one opaque pointer (ADR 0025 D1).

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

/* { dg-do compile } */
/* { dg-options "-fcrug-channels -fcrug-ownership" } */

struct pair { int a; int b; };

int
main (void)
{
  chan int a = channel (1);
  chan double b = channel (1);
  chan struct pair c = channel (1);
  chan own char *d = channel (1);

  /* The element type never reaches the representation: a channel is a
     pointer, so its size and alignment are the target's pointer ones and
     there is no element-dependent layout to freeze.  */
  _Static_assert (sizeof (a) == sizeof (void *), "channel is a pointer");
  _Static_assert (_Alignof (a) == _Alignof (void *), "channel alignment");
  _Static_assert (sizeof (a) == sizeof (b), "element type changes no size");
  _Static_assert (sizeof (a) == sizeof (c), "struct element changes no size");
  _Static_assert (sizeof (a) == sizeof (d), "ownership changes no size");
  _Static_assert (sizeof (a) == sizeof (void *), "channel is one pointer");

  return 0;
}
