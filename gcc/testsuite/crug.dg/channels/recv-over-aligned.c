/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-channels" } */

/* C-01: the element type may be more strictly aligned than the per-thread
   receive buffer (16 bytes).  A receive of such an element must not be
   lowered to a pointer into the shared buffer: the compiler now fills a
   temporary declared with the element alignment through
   crug_chan_recv_into (void *, void *, size_t).  The run-time proof under
   -fsanitize=undefined is libcrug/tests/channels/recv-align.c, run by
   scripts/check-channels.sh; this test keeps the over-aligned scalar element
   in the compile suite.  */

typedef unsigned long __attribute__ ((aligned (64))) aligned_word;

_Static_assert (_Alignof (aligned_word) == 64, "test premise");

int
main (void)
{
  chan aligned_word c = channel (1);
  aligned_word out;

  send (c, (aligned_word) 0x1234);
  out = recv (c);
  close (c);
  return out != 0x1234;
}
