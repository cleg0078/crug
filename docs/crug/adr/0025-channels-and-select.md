# ADR 0025 - Channels and select: a typed ring, one representation, explicit transfer

- **Status:** Accepted
- **Date:** 2026-10-19
- **Deciders:** project owner (the typed `chan T` surface and the ownership
  rules), compiler maintainer (the select lowering), runtime maintainer (the
  ring and the wait policy)
- **Relates:** ADR 0004 (ownership), ADR 0009 (scheduler model), ADR 0011
  (PAL contract), ADR 0018 D6 (the channel and select grammar), ADR 0023
  (c2crug), ADR 0024 (structured scope), MASTER_SPEC section 29,
  RFC-0001 sections 27 and 31, CRUG_PRODUCTION_ROADMAP sections 16 and 21,
  `docs/crug/CHANNELS.md`

## Context

The grammar for channels and select was fixed in ADR 0018 D6: `chan T` is a
type constructor, `channel (n)`, `send (c, v)`, `recv (c)` and `close (c)`
are the operations, and a `select` statement has four arm forms. What the
grammar does not say is how a channel is represented, what a receive does when
the channel is empty, who owns the value that crosses the channel, and how a
select is allowed to be lowered without a lock in the runtime that the compiler
has to understand.

The specification is deliberately narrow here. RFC-0001 section 31 requires
that a channel operation cannot be used to smuggle a value past the ownership
rules, and the phase-16 non-goals say no timeouts, no cancellation, no
select over a nil channel, and no fairness promise beyond a rotation.

Three facts constrain the design. The ownership pass sees the source, not the
runtime: a value that leaves the caller through `send` must be marked moved by
the pass, which means the compiler has to emit a send the pass can recognise.
The scheduler is cooperative (ADR 0009): a blocking operation has to yield
rather than park a worker. And the runtime must contain no operating-system
primitive (ADR 0011 D1), so a wait is either a yield inside a task or a PAL
sleep on the main thread.

## Decision

### D1 - `chan T` is one opaque pointer for every element type

A channel value has the type `struct crug_chan *`; the element type never
reaches the runtime. `chan T` is recorded at compile time only: it carries
`T` for the parser, the type checker and the ownership pass, and the size of
`T` when the channel is made. The runtime ring stores bytes and knows only
`element_size`.

Consequences: a channel has one ABI row (pointer size and alignment) whatever
`T` is (`docs/crug/ABI.md`), a channel can be a struct member or an array
element without a new layout rule, and no generic machinery is needed. The
element must be a complete type with a size known at compile time
(`crug-CONC-019`); `void` and function types are rejected, and so is an
incomplete struct.

### D2 - `channel (n)` takes its type from the context; capacity 0 is a handoff

`channel (n)` allocates a ring of `n` elements. Because the operation does not
name its element type, the type comes from the declaration initializer, the
return statement, or the element type of the array being initialized
(`crug-CONC-002` when none of those gives a `chan T`). This is the same
context rule the option and result constructors use (ADR 0018 D8), and for the
same reason: the grammar has one form and no type arguments.

Capacity 0 is a synchronous handoff (at most one value in flight); capacity N
buffers N values. `close` is idempotent, a receive drains what was already in
the ring and then returns a zeroed element, and a send on a closed channel is
a fatal runtime error. An owning element is the exception: its zeroed sentinel
is a NULL pointer, which is the loop terminator `for (;;) { own Job *j =
recv (jobs); if (j == NULL) break; ... }`.

### D3 - The ring is a bounded buffer with an atomic head/count, and waits yield

The runtime (`libcrug/core/chan.c`) keeps a ring of `element_size` slots with
a head index and a count, guarded by one atomic word with the same spin pattern
as the scheduler queue. A send waits while the ring is full and a receive waits
while it is empty; inside a task the wait is `crug_task_yield ()`, on the main
thread it is `crug_pal_sleep_ns`, and the number of waits is counted in
`struct crug_chan_stats`. There are no timeouts, no cancellation and no
parking: a blocked task burns scheduler turns, which is acceptable for v1 and
is listed as a limit in `docs/crug/CHANNELS.md`.

The two `_owned` entry points (`crug_chan_send_owned`, `crug_chan_recv_owned`)
are the same code as their plain twins. They exist as a name the compiler
emits for an owning element, and the ownership pass looks those names up to
know that a send transfers the value and a receive produces a value the
receiver must release. The element never carries ownership metadata at run
time.

### D4 - A send of an owning element requires `move`; the receiver owns the value

For a channel whose element owns a resource (`chan own char *`), the value
crosses the channel and the caller must say so: `send (c, move (p))` marks
`p` moved in the ownership pass and the caller's destructor no longer runs
(`crug-CONC-003` otherwise). `adopt` across a channel is rejected
(`crug-CONC-023`): a destructor is analysis metadata, not part of the value,
so it cannot travel; the receiver releases the value itself.

A receive from such a channel produces an owned value with no known destructor;
the receiver can `release` it explicitly or move it on. A `select` arm cannot
bind a receive from an owning-element channel (`crug-CONC-017`): the arm might
not run, so the value would have no owner. Use a plain `recv` for those
channels, and `select` only over channels whose element owns nothing.

### D5 - The select lowering is a rotation plus one readiness probe per arm

`select` lowers to a loop. Before the loop, every arm's channel expression is
evaluated once into an artificial `chan T` variable, so a channel expression
has no side effects that a retried pass could repeat. Each pass picks a start
index from `crug_chan_select_turn ()` (a process-wide counter that advances
once per pass), probes each arm in rotated order with
`crug_chan_recv_ready` / `crug_chan_send_ready`, parks the first ready index
in an artificial `int`, and then dispatches with an if-chain. When nothing is
ready and there is a `default` arm, the default body runs; otherwise the loop
calls `crug_chan_poll_wait ()` and tries again. A send value is evaluated
inside the arm only, so a pass that does not take that arm does not evaluate
it.

**Amendment (2026-10-26).** The claim lowering replaces the readiness probes
with `crug_chan_recv_claim` / `crug_chan_send_claim` (CHANNELS.md section 7),
and D5's last sentence no longer describes it. A select send arm's value
expression is evaluated on **every** claim pass — the claim needs the value to
attempt the send — and it is evaluated again in the closed-send abort path
(`crug_channel_send_closed_abort`) before that body runs. The expression must
therefore be side-effect-free apart from the move the arm performs; a select
send arm whose value expression mutates state is a stale-side-effect bug, and
the compiler does not diagnose it.

The lowered statements are built with raw `build2`/`build3` expressions and
`WHILE_STMT`/`BREAK_STMT` nodes, never with `build_binary_op`, because the
latter wraps a condition in `C_MAYBE_CONST_EXPR` that generic gimplification
cannot lower (the same lesson as the match lowering, ADR 0018 D8). The
ownership pass already walks every node the lowering produces, so a `break`,
a `return` or a `goto` out of an arm collects exactly that arm's drops.

The rotation is the whole fairness policy: no queue of waiters, no promise that
a ready arm is taken in declaration order, and no priority.

### D6 - Everything the runtime offers is additive to runtime ABI version 1

`libcrug/core/chan.h` is a new set of entry points; no existing signature
changed (`docs/crug/ABI.md` section 6). The channel object is opaque and its
address is the only thing that crosses a Crug-to-C boundary. The compiler emits
its own declarations for the runtime functions it calls, exactly as it does for
`crug_task_spawn`, so a Crug translation unit needs no header.

## Consequences

- A channel is a pointer, so `chan T` is trivially copyable and passable to a
  function by value; the object it points at is shared, and the compiler does
  not attempt to prove anything about it.
- A select over an owning-element channel is deliberately not expressible;
  fan-in of owned jobs uses a plain `recv` loop.
- The ring never grows, and a channel object is never freed in v1. That keeps a
  receive after `close` well defined and keeps a leak checker honest, at the
  cost of a small permanent allocation per channel.
- `crug_chan_select_turn` is process-wide, so two threads lowering selects at
  the same time share the rotation; the result is still fair over time, just not
  per thread.
- A blocked send or receive is a spin with a yield; there is no parked state, so
  a program with more blocked tasks than workers can starve. This is the same
  cooperative limit the scheduler already has (ADR 0009 D3).

## Alternatives considered

- **A generic channel object with a per-element vtable.** Rejected: it would put
  ownership and destruction into the runtime, which the ownership analysis owns,
  and it would make the ABI depend on the element type.
- **Lowering select to a runtime call that blocks until one arm is ready.**
  Rejected: the arm bodies are compiler-generated code, so the runtime would
  have to run closures; the readiness-probe loop keeps the runtime type-blind.
- **Making `send` of an owning element implicit (no `move`).** Rejected: the
  ownership pass could not tell a transfer from an aliasing copy, which is the
  hole ADR 0004 exists to close.
- **A select over owned receives with a drop for the arms not taken.**
  Rejected for v1: it needs a per-arm drop path that the current lowering does
  not have, and it would be the only place where a select arm owns a value.
