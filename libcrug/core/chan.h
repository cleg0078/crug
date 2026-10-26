/* chan.h - the Crug channel runtime, Phase 16 (16A).
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   A channel is a type-erased ring of values with a capacity.  The compiler
   lowers

     chan int c = channel (4);
     send (c, 7);
     int v = recv (c);
     close (c);

   to the calls below.  The element type never reaches the runtime: every
   value is copied through a void * and the element size is fixed when the
   channel is made, so a channel object has one representation for all
   element types (docs/crug/ABI.md).

   Two pairs of entry points carry the same code on purpose:
   crug_chan_send_owned() and crug_chan_recv_owned() are the ones the
   compiler emits when the element type is an owned pointer.  The ownership
   analysis (docs/crug/CHANNELS.md) looks up those names to know that a send
   transfers the value out of the caller and that a receive produces a value
   the receiver must release.

   Blocking: a task waits by yielding to the scheduler, the main thread by
   sleeping in the PAL, so no operating system primitive appears here
   (ADR 0011 D1).  There are no timeouts and no cancellation in v1.

   Capacity 0 is synchronous handoff: at most one value is in flight, a send
   waits for a receiver to take it, and a receive waits for a sender.  A
   channel with capacity N holds N values between the sender and the
   receiver.

   close () sets a closed flag and is idempotent.  A receive drains the
   values that were already in the channel, then returns a zeroed element
   (a NULL pointer for an owned element), which is the sentinel the loop

     for (;;) { own Job *j = recv (jobs); if (j == NULL) break; ... }

   relies on.  A send on a closed channel is a fatal runtime error, the same
   way a spawn failure is.  A channel object is never freed in v1: the
   registry below keeps every channel reachable, so a receive after close is
   always well defined and a leak checker sees no lost block.
*/

#ifndef CRUG_CHAN_H
#define CRUG_CHAN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create a channel of capacity values of element_size bytes.  capacity 0 is
   a synchronous handoff.  Never returns NULL: creating a channel is not
   allowed to fail silently (the process dies with a diagnostic instead).  */
void *crug_chan_make (size_t capacity, size_t element_size);

/* Send one element.  send_owned is the form the compiler emits for an owned
   element type; both copy the element and block while the channel is full.  */
void crug_chan_send (void *chan, const void *element);
void crug_chan_send_owned (void *chan, const void *element);

/* Receive one element into a per-task buffer (a per-thread buffer on the main
   thread) and return a pointer to it.  The pointer is valid until the next
   receive on the same task or thread: two tasks that share a worker never
   share the buffer.  On a closed and empty channel the buffer holds a zeroed
   element.  */
void *crug_chan_recv_ptr (void *chan);
void *crug_chan_recv_owned (void *chan);

/* The non-blocking claim pair the select lowering uses.  recv_claim returns 1
   and copies the element into out when one is available, 0 when a blocking
   receive would wait, and -1 when the channel is closed and drained (out is
   then a zeroed element).  send_claim returns 1 when the value was accepted,
   0 when a blocking send would wait, and -1 when the channel is closed.  For a
   capacity-0 channel "accepted" means the value was handed to a receiver that
   was already waiting, so a claim that returns 1 has completed the handoff.
   Neither call yields on 0; the select calls crug_chan_poll_wait() and retries.
   size is the caller's element size and must match the channel's.  A claim
   never uses the shared receive buffer, so a losing select arm cannot overwrite
   a value another arm is examining.  */
int crug_chan_recv_claim (void *chan, void *out, size_t size);
int crug_chan_send_claim (void *chan, const void *element, size_t size);
/* The compiler emits the ownership-analysis spelling for an owned send arm;
   the code is identical to crug_chan_send_claim().  */
int crug_chan_send_owned_claim (void *chan, const void *element, size_t size);

/* Non-blocking readiness, the poll behind 'select'.  A closed channel is
   always ready for a receive (it yields the zeroed sentinel) and ready for a
   send only in the sense that a send would abort; the select lowering checks
   the receive side.  */
int crug_chan_recv_ready (void *chan);
int crug_chan_send_ready (void *chan);

/* Let the scheduler run, then come back: the body of a blocked select.  */
void crug_chan_poll_wait (void);

/* The select rotation: a process-wide counter that advances once per select
   pass, so the arm a select probes first changes from one pass to the next
   (fairness, docs/crug/CHANNELS.md).  */
unsigned crug_chan_select_turn (void);

/* Mark the channel closed and idempotent.  A send after this is fatal.  */
void crug_chan_close (void *chan);

/* Introspection for tests and for the diagnostics of a stuck program.  */
size_t crug_chan_len (void *chan);
size_t crug_chan_cap (void *chan);

struct crug_chan_stats
{
  uint64_t created;   /* channels made                                */
  uint64_t live;      /* channels made and not released (always == created) */
  uint64_t sent;      /* elements sent                                */
  uint64_t received;  /* elements received                            */
  uint64_t wait;      /* times a sender or receiver had to wait       */
};

void crug_chan_stats_get (struct crug_chan_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* CRUG_CHAN_H */
