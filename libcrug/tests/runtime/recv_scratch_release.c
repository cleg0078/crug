/* recv_scratch_release.c - the main-thread receive slot is released at thread
   exit (K-031, PAL contract 1.3).

   Every receiver outside a task gets one grow-only buffer in a per-thread
   slot.  Before PAL 1.3 the slot had no release entry point, so a short-lived
   receiver thread leaked it (documented in scripts/check-sanitizer.sh).  Now
   chan.c arms crug_pal_thread_at_exit() when it first creates the slot, so a
   receiver that exits frees its buffer: this program runs many such receivers
   and a user hook registered before the receive proves the exit chain fired
   (LIFO: the slot hook, armed during the receive, runs first, then this
   hook).  The leak itself is witnessed by the LeakSanitizer arm of
   scripts/check-sanitizer.sh, whose stress receiver now exits.

   Both backends run it: the fake fires the hooks when a fake thread's entry
   returns inside crug_pal_thread_join, Linux through the key destructor, and
   the join-then-observe order is the same on both.

   Print one line and exit 0 on success, or print FAIL lines and exit 1.
*/

#include "chan.h"
#include "crug_pal.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROUNDS 64
#define SMALL 8u
#define BIG 64u

static void *small_chan;
static void *big_chan;
static _Atomic unsigned int released;	/* receiver exits observed	*/
static _Atomic unsigned int values_ok;	/* elements read back intact	*/
static _Atomic unsigned int hook_fail;	/* a user hook never fired	*/

/* Registered before the first receive, so the slot hook (armed during the
   receive, LIFO later) has already run when this one runs: observing it
   after join proves the whole exit chain, including the slot release, fired
   before the join could return.  */
static void
note_exit (void *arg)
{
  (void) arg;
  atomic_fetch_add_explicit (&released, 1, memory_order_release);
}

static void
receive_and_exit (void *opaque)
{
  unsigned int round = (unsigned int) (size_t) opaque;
  unsigned char expect[64];
  unsigned char *got;
  size_t size = (round % 2) ? BIG : SMALL;

  if (crug_pal_thread_at_exit (note_exit, opaque) != 0)
    {
      atomic_store_explicit (&hook_fail, 1, memory_order_relaxed);
      return;
    }
  memset (expect, (int) (round + 1), size);
  /* Grow-only: an odd round asks for the big element, so a later even round
     reuses the big buffer; each fresh thread must allocate a fresh slot.  */
  got = (unsigned char *) crug_chan_recv_ptr ((round % 2) ? big_chan : small_chan);
  if (got != NULL && memcmp (got, expect, size) == 0)
    atomic_fetch_add_explicit (&values_ok, 1, memory_order_relaxed);
}

int
main (void)
{
  unsigned int round;

  small_chan = crug_chan_make (1, SMALL);
  big_chan = crug_chan_make (1, BIG);

  for (round = 0; round < ROUNDS; round++)
    {
      crug_pal_thread *thread = NULL;
      unsigned char payload[64];
      int rc;

      memset (payload, (int) (round + 1), BIG);
      crug_chan_send ((round % 2) ? big_chan : small_chan, payload);
      rc = crug_pal_thread_create (&thread, receive_and_exit,
                                   (void *) (size_t) round);
      if (rc != 0 || thread == NULL)
	{
	  fprintf (stderr, "FAIL: recv-scratch-release: create failed (%u)\n", round);
	  return 1;
	}
      if (crug_pal_thread_join (thread) != 0)
	{
	  fprintf (stderr, "FAIL: recv-scratch-release: join failed (%u)\n", round);
	  return 1;
	}
      /* The join returned: a backend that honours the 1.3 hook must already
	 have released this receiver's slot and run the user hook.  */
      if (atomic_load_explicit (&released, memory_order_acquire) != round + 1)
	{
	  fprintf (stderr, "FAIL: recv-scratch-release: the exit hook had not run"
			   " by the time join returned (%u)\n", round);
	  return 1;
	}
    }

  if (atomic_load_explicit (&values_ok, memory_order_acquire) != ROUNDS)
    {
      fprintf (stderr, "FAIL: recv-scratch-release: %u of %u elements intact\n",
	       atomic_load_explicit (&values_ok, memory_order_acquire), ROUNDS);
      return 1;
    }
  if (atomic_load_explicit (&hook_fail, memory_order_acquire) != 0)
    {
      fprintf (stderr, "FAIL: recv-scratch-release: a user exit hook misfired\n");
      return 1;
    }
  printf ("recv-scratch-release: rounds=%u released=%u values=%u\n",
	  ROUNDS, atomic_load_explicit (&released, memory_order_acquire),
	  atomic_load_explicit (&values_ok, memory_order_acquire));
  return 0;
}
