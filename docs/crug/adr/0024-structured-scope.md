# ADR 0024 - Structured scope: a task group that outlives no block

- **Status:** Accepted
- **Date:** 2026-10-19
- **Deciders:** project owner (the scope-is-a-task-group shape and the
  no-cancellation non-goal), compiler maintainer (the lowering and the
  goto rules), runtime maintainer (the group object and the wait policy)
- **Relates:** ADR 0008 (goroutine stack model), ADR 0009 (scheduler model),
  ADR 0013 (arch context switch), ADR 0020 (last-use liveness),
  MASTER_SPEC section 32, RFC-0001 sections 27 and 29,
  CRUG_PRODUCTION_ROADMAP section 20, `docs/crug/SCOPE.md`

## Context

RFC-0001 section 29 gives one structural rule and one consequence: a child task
never outlives the scope that started it, and a scope must not complete normally
until its required children have completed. MASTER_SPEC section 32 calls the
scope "semantically important" and sketches create group / spawn / wait all /
destroy. The phase-15 non-goals are equally clear: no full cancellation trees,
no error aggregation beyond a baseline, no detached tasks, no task-local
storage, and no channels or select (those are Phase 16).

Nothing in the specification says how a group is represented, what a spawn into
a group does, what the wait costs, or what a borrow captured by a scoped child
is allowed to point at. Those are the decisions this record fixes.

The runtime already has an M:N scheduler (ADR 0009) with one ready queue,
cooperative scheduling and an epoch wait predicate, and the compiler already
lowers `go f (args);` into a frame plus a thunk plus `crug_task_spawn`
(Phase 14). Structured scope has to be a small extension of both, not a second
scheduler.

## Decision

### D1 - The scope statement is a task group, entered and left by the block

`scope { ... }` lowers to a group handle created by `crug_scope_enter()` at
block entry and released by `crug_scope_leave(handle)` on every path out of
the block. The handle is an artificial `void *` variable declared in the
block; only the two runtime calls may touch it. The runtime object is opaque
(`struct crug_scope`) and is not the scheduler's queue or a coroutine object:
it carries the pending count of its children and a magic word for misuse.

### D2 - A scoped spawn joins the group; completion wakes the group

`crug_task_spawn_scoped(group, entry, frame, size)` increments the group's
pending count **before** the task is queued, so the waiter can never observe an
empty group while a task for it is still being built. When a child returns, the
runtime decrements the count after the task body has finished but before the
task's stack is released, and wakes every waiter when the count reaches zero.
A NULL handle is defined as `crug_task_spawn`: the compiler only emits the
scoped form inside a scope, but the fallback keeps the runtime total.

### D3 - Waiting yields inside a task and uses the PAL on the main thread

`crug_scope_leave` waits until the pending count is zero. Inside a task it
calls `crug_task_yield()` in a loop, so the worker keeps running other tasks
and the very children being waited for can make progress; on the main thread it
waits with `crug_pal_wait_u32` on the count. Blocking a worker on a group
would starve the children of that group under a small worker count, which is
exactly the M:N failure the scheduler is meant to avoid.

There is no WAITING task state in v1: a waiting task consumes scheduler turns
while it spins. That is a documented cost, not an invariant; a parked state is
future work and does not change the surface described here.

### D4 - A scoped child may capture a borrow of a value that outlives the block

Outside a scope, a borrow captured by an unstructured task is rejected
(`crug-CONC-012`): no scope guarantees the child is done before the loan ends.
Inside a scope the guarantee exists, so a direct `borrow(...)` or
`borrow_mut(...)` argument is accepted when the lender is declared at the
scope body's own block depth or shallower. Two further rules follow from the
children running concurrently:

- a lender declared in a block that ends before the scope does is rejected
  (`crug-CONC-016`), because the child could still read it after it died;
- two children of one scope may not hold a mutable borrow of the same lender
  (`crug-BORROW-003`), and a mutable and a shared borrow of one lender in one
  scope conflict as well (`crug-BORROW-002`).

A borrow that is reborrowed or stored in a local first is not followed into the
scope; the capture must name the borrow directly. Owned captures keep the Phase
14 rules (`move` transfers, a destructor cannot cross a task boundary).

### D5 - The compiler leaves the group on every exit, and rejects a goto in

The ownership pass already collects the destructions of every frame reached by
an exit. A scope block is a frame with a wait "destruction" that is emitted
**before** that frame's own destructions, at the normal end of the block and on
`return`, `break`, `continue` and `goto` out. The order is load-bearing: a
child may still be using an object the block owns, so the block must wait before
it destroys anything.

A `goto` **into** a scope body is rejected (`crug-CONC-015`): it would skip
the group's creation and every exit from that body would leave an uninitialized
handle. Jumping out is legal, because the target is outside the block and the
wait is emitted on the way.

### D6 - The scope is a contextual word and the surface stays additive

`scope` is a contextual word like `go` and `yield`: it starts a scope
statement only when it is an undeclared identifier directly followed by `{`.
A declared `scope` (`int scope = 11;`) and a `scope:` label keep their C
meaning, with the flag on or off. A bare `scope;` is an ordinary expression
statement over an undeclared identifier.

The compiler also needs a handle in the block's variable list: a block that
declares nothing is otherwise lowered to a bare statement list, and the pass
would have no frame to attach the wait to. The handle is artificial and
ignored, so it does not change the program's observable behaviour.

The runtime surface is additive: the Phase 14 entry points keep their
signatures and the runtime ABI version stays 1. `crug_scope_stats`
(`entered`, `left`, `live`) is a diagnostic surface for the gate, not a
scheduling API.

### D7 - Non-goals of v1

No cancellation trees, no error aggregation (there is no result channel for a
task yet), no detached tasks, no task-local storage, no channels or select, no
nested group objects (nesting is per block and the inner block waits first), and
no scope ABI promise beyond version 1. A scope inside a task waits by yielding;
a scope on the main thread waits in the PAL.

## Consequences

- A child of a scope is provably finished before the block ends; the counters
  `spawned`/`completed` remain lifetime counters, while the group counters
  are per process and must balance.
- A borrow captured by a scoped child is sound under the same rule that makes a
  local borrow sound: the loan ends at last use inside a block the child cannot
  outlive.
- The wait is cooperative: a program whose child never returns hangs in
  `crug_scope_leave`, exactly as a program with an infinite loop does. A
  cancellation baseline is a later decision, not a hidden timeout.
- The group object is per process accounting: `crug_scope_stats` must report
  `live == 0` after every scope has been left, which is what the stress gate
  reads.

## Alternatives considered

- **A scheduler-level wait state.** Correct, but it changes the scheduler model
  recorded in ADR 0009 and is not needed to make the structural guarantee;
  deferred.
- **Letting the parser emit the wait only at the block end.** Misses `return`
  and `goto` out of the block, which is the common early-exit shape; rejected.
- **Treating the scope as a lexical block and tracking children in a list on
  the scheduler.** Duplicates the pending counter the group already has, and
  makes "how many children are left" a scan; rejected.
- **Allowing any borrow capture inside a scope, including a lender from an
  inner block.** Unsound: the child outlives the inner block; rejected in favour
  of `crug-CONC-016`.

## Tests

- `gcc/testsuite/crug.dg/scope/` (14 fixtures): the forms, nesting, every exit,
  the gated-off keyword, the label and the variable shadow, plus the rejections
  (`crug-CONC-015`, `crug-CONC-016`, `crug-BORROW-002`, `crug-BORROW-003`,
  `crug-CONC-012`).
- `libcrug/tests/scope/` (8 programs) compiled by `scripts/check-scope.sh`:
  wait-for-all, scoped borrow, owned capture, early return, break in a loop,
  nesting, a scope inside a task, and the group stress with
  `leaked task group state: 0`.
- `libcrug/tests/scope_stress.c` drives the group API without the statement and
  runs under ASan/UBSan/LeakSanitizer in `scripts/check-tasks-sanitizer.sh`.
- `make check-SCOPE` runs the DejaGnu suite, the eight programs and the group
  counters; the phase-15 gate reads its summary line.
