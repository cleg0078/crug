/* recv_into.c - C-01 runtime half: receive directly into caller storage.

   The channel element is over-aligned (64 bytes).  The receive-into forms must
   write through the caller's pointer (which carries the element type's
   alignment) and never allocate or expose the shared per-task scratch.  The
   pointer forms stay behaviour-compatible for recv_claim/select.  */
#define _DEFAULT_SOURCE 1

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chan.h"
#include "task.h"

#include "crug_pal.h"

struct __attribute__ ((aligned (64))) elem
{
  unsigned char pad[64];
  unsigned long tag;
};

static void *task_chan;
static _Atomic unsigned task_ok;

static void
fail (const char *what)
{
  fprintf (stderr, "FAIL: recv-into: %s\n", what);
  _Exit (1);
}

static void
task_receive (void *frame)
{
  void *scratch;
  struct elem out;

  (void) frame;
  scratch = crug_task_recv_scratch_aligned (4096u, 64u);
  if (scratch == NULL || (uintptr_t) scratch % 64u != 0u)
    fail ("task scratch is not 64-byte aligned");
  memset (scratch, 0xab, 4096u);

  memset (&out, 0, sizeof out);
  if (crug_chan_recv_into (task_chan, &out, sizeof out) != 1)
    fail ("task receive-into did not return 1");
  if (out.tag != 0x77u)
    fail ("task receive-into value mismatch");
  atomic_store_explicit (&task_ok, 1u, memory_order_release);
}

int
main (void)
{
  struct elem in;
  struct elem out;
  _Alignas (64) unsigned char raw[sizeof (struct elem)];
  void *chan;
  struct crug_task_stats stats;
  int i;

  setenv ("CRUG_TASKS_WORKERS", "2", 1);
  chan = crug_chan_make (2u, sizeof (struct elem));

  memset (&in, 0, sizeof in);
  in.tag = 0x11u;
  crug_chan_send (chan, &in);
  in.tag = 0x22u;
  crug_chan_send (chan, &in);

  if (crug_chan_recv_into (chan, raw, sizeof raw) != 1)
    fail ("receive-into did not return 1");
  memcpy (&out, raw, sizeof out);
  if (out.tag != 0x11u)
    fail ("receive-into value mismatch");

  memset (&out, 0xff, sizeof out);
  if (crug_chan_recv_into_owned (chan, &out, sizeof out) != 1)
    fail ("receive-into-owned did not return 1");
  if (out.tag != 0x22u)
    fail ("receive-into-owned value mismatch");

  crug_chan_close (chan);
  memset (&out, 0xff, sizeof out);
  if (crug_chan_recv_into (chan, &out, sizeof out) != 0)
    fail ("closed receive-into did not return 0");
  for (i = 0; i < (int) sizeof out; i++)
    if (((unsigned char *) &out)[i] != 0u)
      fail ("closed receive-into did not zero the destination");

  if (crug_chan_recv_ptr (chan) == NULL)
    fail ("the pointer form no longer returns a value");
  if (((const struct elem *) crug_chan_recv_ptr (chan))->tag != 0u)
    fail ("the pointer form did not return the zeroed sentinel");

  task_chan = crug_chan_make (1u, sizeof (struct elem));
  in.tag = 0x77u;
  crug_chan_send (task_chan, &in);
  crug_task_spawn (task_receive, NULL, 0);

  for (i = 0; i < 5000; i++)
    {
      crug_task_stats_get (&stats);
      if (stats.completed == 1u && atomic_load_explicit (&task_ok,
							 memory_order_acquire))
	break;
      crug_pal_sleep_ns (1000000ull);
    }
  if (i == 5000)
    fail ("task did not receive into its aligned storage");
  if (crug_task_runtime_try_shutdown () != 0)
    fail ("runtime did not stop");

  printf ("recv-into: into=1 owned=1 closed=0 zeroed=1 ptr=1 task=1 "
	  "align64=1 peak=%u\n", stats.queue_peak);
  return 0;
}
