# Channels and select

Status: v1 implemented 2026-10-19 (Phase 16). Decisions are recorded in
[ADR 0025](adr/0025-channels-and-select.md); the runtime API is
[`libcrug/core/chan.h`](../../libcrug/core/chan.h).

A channel is a typed queue that carries values between tasks. `select` waits on
several channel operations at once. Both are gated by `-fcrug-channels`;
without it `chan` is an unknown type name and `send`/`recv`/`close` are
ordinary calls or implicit declarations.

## 1. The surface

```c
chan int ch = channel (4);
send (ch, 7);
int v = recv (ch);
close (ch);
```

- `chan T` is a type constructor: the element type is a type-name, and
  `chan` may be spelled with `own` inside the element (`chan own char *`).
  A `typedef chan int int_chan;` is allowed, and so is an array of channels.
- `channel (n)` makes a channel of capacity `n`; `n` is evaluated once
  and never changed. Capacity 0 is a synchronous handoff: a send waits for a
  receiver and a receive waits for a sender.
- `send (ch, v)` blocks while the channel is full, `recv (ch)` blocks while
  it is empty, and `close (ch)` is idempotent. A receive from a closed channel
  drains what was already queued and then returns a zeroed element (a NULL
  pointer for an owning element).
- `send`, `recv`, `close` and `channel` are contextual words, and the
  operation is chosen by its first argument (ADR 0018 D6 R1):
  `send (ch, v)`, `recv (ch)` and `close (ch)` apply whenever the first
  argument has a channel type, even if a function of that name is declared.
  A declared function keeps its C meaning for every other argument, so
  `close (fd)` from `<unistd.h>` still closes a file descriptor in a
  translation unit that also closes a channel. A user function whose first
  parameter is a channel type always wins over the operation.
- `select` is recognized even when the translation unit declares POSIX
  `select`: no C statement can put a block after an identifier, so
  `select {` is unambiguous (ADR 0002 D4). A variable named `select` or
  `scope` is still readable and assignable.
- `select` is a statement with four arm forms:

```c
select {
    case v = recv (a):          /* receive into a new binding */
       use (v);
   case recv (b):              /* receive, value discarded         */
       note ();
   case send (c, 7):           /* send when the channel has room   */
       count ();
   default:                    /* at most one, must be last        */
       idle ();
}
```

An arm body is one or more statements. `default` may be omitted; it may appear
at most once (`[crug-CONC-005]`) and must be the last arm
(`[crug-CONC-006]`). `case v == recv (a)` is a common mistake and has its
own diagnostic (`[crug-CONC-007]`).

## 2. Lowering

`chan T` is one opaque pointer at run time. The compiler only records `T`:
its size is what `channel (n)` passes as the element size, and it is what
`recv` copies back. The generated code names the runtime entry points itself,
so a Crug translation unit needs no header:

```c
chan int ch = channel (4);   /* struct crug_chan *ch = crug_chan_make (4, sizeof (int)); */
send (ch, 7);                /* crug_chan_send (ch, &(int){ 7 });                        */
int v = recv (ch);           /* int v = *(int *) crug_chan_recv_ptr (ch);                */
close (ch);                  /* crug_chan_close (ch);                                    */
```

A `select` lowers to a loop, not to a runtime call:

```c
chan int __ch0 = a;          /* each channel expression is evaluated once */
chan int __ch1 = b;
for (;;)
  {
    int __sel = -1;
    int __start = (int) crug_chan_select_turn () % 2;
    int __i = 0;
    while (__i < 2 && __sel < 0)
      {
        int __j = (__start + __i) % 2;
        if (__j == 0) { if (crug_chan_recv_ready (__ch0)) __sel = 0; }
        else          { if (crug_chan_recv_ready (__ch1)) __sel = 1; }
        __i = __i + 1;
      }
    if (__sel == 0) { /* arm 0 */ break; }
    else if (__sel == 1) { /* arm 1 */ break; }
    else { /* default body, or */ crug_chan_poll_wait (); }
  }
```

The rotation counter advances once per pass, so the arm probed first changes
from one pass to the next. A send value is evaluated inside its arm only. The
lowered nodes are `WHILE_STMT`, `COND_EXPR`, `BREAK_STMT` and raw
`build2` expressions, which the ownership pass already walks, so leaving an
arm by `break`, `return` or `goto` collects that arm's destructions.

## 3. Ownership

A channel is a shared object; the values in it are not shared. The rules the
compiler enforces:

| Situation | Result |
|-----------|--------|
| `chan own T`, `send (ch, move (p))` | accepted: the value is the receiver's |
| `chan own T`, `send (ch, p)` without `move` | `[crug-CONC-003]` |
| `send (ch, adopt (p, dtor))` | `[crug-CONC-023]`: a destructor cannot cross a channel |
| `own T *p = recv (ch)` on an owning-element channel | accepted: the value is owned, with no known destructor (`release` it or move it on) |
| `select { case v = recv (ch): ... }` on an owning-element channel | `[crug-CONC-017]`: the arm might not run |
| `channel (n)` with no channel type in the context | `[crug-CONC-002]` |
| element type is `void`, a function or incomplete | `[crug-CONC-019]` |
| element type is not `Send` (a raw pointer, a pointer-holding struct) | `[crug-CONC-027]`; annotate the pointee in a `.crugffi` to widen it |
| a sent value whose type is not `Send` | `[crug-CONC-024]` |
| a thread-affine value sent on a channel | `[crug-CONC-025]` |

A channel whose element owns nothing (every value type, a struct of such
values) carries no ownership at all: `send` copies the value and the sender
keeps its own. A plain pointer is not a value in this sense: it is not `Send`,
so a `chan char *` is rejected until the sidecar annotates the pointee
(`SEND-SYNC.md`, ADR 0026 D2).

## 4. Runtime API

```c
void *crug_chan_make (size_t capacity, size_t element_size);
void  crug_chan_send (void *chan, const void *element);
void  crug_chan_send_owned (void *chan, const void *element);
void *crug_chan_recv_ptr (void *chan);
void *crug_chan_recv_owned (void *chan);
int   crug_chan_recv_ready (void *chan);
int   crug_chan_send_ready (void *chan);
void  crug_chan_poll_wait (void);
unsigned crug_chan_select_turn (void);
void  crug_chan_close (void *chan);
size_t crug_chan_len (void *chan);
size_t crug_chan_cap (void *chan);

struct crug_chan_stats { uint64_t created, live, sent, received, wait; };
void crug_chan_stats_get (struct crug_chan_stats *out);
```

- A receive returns a pointer to a per-thread buffer that is valid until the
  next receive on that thread; the compiler copies the element out immediately.
- This surface is part of runtime ABI version 1 and is OS- and
  architecture-independent (Phase 18, ADR 0012): the same `chan.c` builds on
  Linux x86-64, Linux AArch64, Windows x86-64 and macOS ARM64, and only the PAL
  wait/wake mapping differs.
- `crug_chan_recv_ready` is true when a receive would not block: a value is
  queued or the channel is closed. A closed channel is not "ready to send":
  a send on it is a fatal runtime error.
- A blocked send or receive waits by `crug_task_yield ()` inside a task and by
  `crug_pal_sleep_ns` on the main thread, so no operating-system primitive
  appears in the runtime (ADR 0011 D1).
- `live` counts channels made and not released. A channel object is never
  freed in v1, so `live == created`; the counters are for tests and for the
  diagnostic of a stuck program.

The runtime ABI version stays 1: the Phase 14/15 entry points keep their
signatures and `chan.h` is additive.

## 5. Limits

- No timeouts, no cancellation, no context: a `select` with no ready arm and
  no `default` spins until one is.
- No selection over a nil channel and no close of a nil channel (there is no
  nil channel value: `NULL` is not a `chan T`).
- Fairness is the rotation only: no waiter queue, no priorities, no promise
  that arm order is declaration order.
- `select` cannot receive from an owning-element channel; use a plain `recv`.
- A send on a closed channel is fatal (the process aborts), not an error code.
- A blocked task burns scheduler turns instead of being parked; a channel
  object lives for the process lifetime.
- No `select` over plain function calls, no dynamic arm count, no reflection.

## 6. Tests

| Command | What it proves |
|---------|----------------|
| `make check-CHANNELS` | all of the below; prints `channels: 40 compiler test(s), 7 channel program(s), 0 failure(s)` |
| `make check-CHANNELS_DG` | `gcc/testsuite/crug.dg/channels/` runs under DejaGnu (24 fixtures) |
| `scripts/check-channels.sh` | builds and runs `libcrug/tests/channels/*.c` against the Linux runtime |
| `scripts/check-diagnostics.sh` | `CONC-001` to `CONC-009` and `CONC-017` to `CONC-023` are registered and cited |

The run cases under `libcrug/tests/channels/` and the line each prints:

| Program | Output |
|---------|--------|
| `roundtrip.c` | `channels-roundtrip: values=4 sum=10 closed=0` |
| `rendezvous.c` | `channels-rendezvous: handoffs=3 total=6` |
| `select.c` | `channels-select: a=33 b=63 defaults=0` |
| `select-send.c` | `channels-select-send: sent=1 got=111` |
| `owned.c` | `channels-owned: text=hi releases=1` |
| `close.c` | `channels-close: a=5 b=6 tail=0` |
| `fanin.c` | `channels-fanin: producers=5 total=20` |

The compile-only fixtures cover the type surface (typedef, array, an object
returning a channel), the four select arm forms, the ownership rules and every
one of the seven new diagnostics.

## 7. Post-rc1 changes: tickets and claims (2026-10-26)

Capacity-0 completion no longer reads the shared slot as a proxy for "my value
was taken". Each unbuffered send takes a ticket (`next_ticket`, published as
`inflight_ticket`) and completes only when the receiver sets
`last_consumed` to that ticket; the receiver consumes the in-flight ticket
under the channel lock. Two senders therefore cannot steal each other's
completion, and closing a channel mid-handoff is fatal instead of a lost wake.
`close` sets the closed flag and buffered values still drain after it.

The polled select protocol gained a non-blocking claim pair:
`int crug_chan_recv_claim (void *handle, void *out, size_t size)` and
`int crug_chan_send_claim (void *handle, const void *value, size_t size)`
(plus the owned send form), each returning 1 when the operation completed,
0 when a blocking call would wait, and -1 when the channel is closed; a
receive claim zero-fills its output on -1 and never uses thread-local storage.
`select` claims each arm at most once in rotation order and the first nonzero
claim wins; the send-arm value expression is evaluated on every claim pass; a
closed send arm emits `crug_channel_send_closed_abort` before its body; the
old `crug_channel_ready` probe is gone. Receiving an owning channel stays
rejected (`crug-CONC-017`). Limitation, stated not discovered: a claim send
and a claim receive cannot rendezvous on capacity 0; pair a claim send with a
blocking receive, and a claim receive with a blocking or buffered send.
These entry points are additive; the runtime ABI stays 1.

Staleness note (ADR 0025 D5 amendment): because a send arm is claimed, not
probed, its value expression is evaluated on every claim pass and again in the
closed-send abort path, so it must be side-effect-free apart from the move.
Known limitation of the conservative lowering: a select arm that sends an owned
value is left in place rather than transferred, so the ownership pass can report
a false `OWN-007` (or a `DROP-006` for the not-taken path); the value is not
double-freed, but the diagnostic is conservative.
