/* recv-align.c - a receive of an over-aligned scalar goes to aligned storage.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

/* The element is more strictly aligned than the 16-byte receive scratch the
   runtime keeps for compatibility.  The compiler must receive into a temporary
   declared with this alignment (C-01), not into that scratch.  */
typedef unsigned long __attribute__ ((aligned (64))) aligned_word;

static void
producer (chan aligned_word ch)
{
  send (ch, (aligned_word) 0x1234);
}

int
main (void)
{
  chan aligned_word ch = channel (1);
  aligned_word got;

  scope {
    go producer (ch);
  }

  got = recv (ch);
  crug_task_runtime_shutdown ();

  printf ("channels-recv-align: value=%lu align=%zu size=%zu\n",
          (unsigned long) got, (size_t) _Alignof (aligned_word),
          sizeof (aligned_word));
  return (got == (aligned_word) 0x1234
          && _Alignof (aligned_word) == 64
          && sizeof (aligned_word) == 8) ? 0 : 1;
}
