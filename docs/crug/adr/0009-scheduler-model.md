# ADR 0009 - Scheduler model: M:N, one ready queue, cooperative

- **Status:** Accepted
- **Date:** 2026-10-19
- **Deciders:** project owner (cooperative scheduling and the worker default),
  runtime maintainer (the queue and the shutdown protocol)
- **Relates:** ADR 0008 (goroutine stack model), ADR 0011 (PAL contract), ADR
  0013 (arch context switch), MASTER_SPEC sections 25.1-25.6 and 27, RFC-0001
  sections 6 and 35, CRUG_PRODUCTION_ROADMAP section 19,
  `docs/crug/TASKS.md`, `docs/crug/GOROUTINES.md`

## Context

MASTER_SPEC section 25.1 states the model in one sentence: a goroutine is not an
operating-system thread, M tasks run on N workers, M >> N. Section 27 fixes the
source-level rules for `go`. Section 79 requires this ADR by name. The roadmap
adds acceptance numbers: 100000 trivial tasks, no task loss, no stack
corruption, sanitizer-clean, and a plain-C or kernel build that links no
runtime.

What the specification leaves open is the shape of the queue, the worker
default, how a task sleeps and wakes, what shutdown means, and what happens
when the machine cannot start a worker.

## Decision

### D1 - M:N with one global FIFO ready queue, no stealing, no priorities

Workers come only from `crug_pal_thread_create` (ADR 0011 D1). The default
worker count is `min (usable processors, 8)`, clamped to 1..64 and overridable
with `CRUG_TASKS_WORKERS` before the first spawn. One global run queue guarded
by an atomic spin lock serves every worker: FIFO order, no work stealing, no
priorities and no preemption. The spin lock is safe because every critical
section is a handful of pointer updates; the cost is contention at high worker
counts, which is the first thing a later phase may revisit.

### D2 - Three states, and a transition only through the worker

A task is READY, RUNNING or DONE. Only the worker that runs a task changes its
state, and a yielded task is requeued **after** the switch back to the worker.
That ordering is the memory-ordering argument: the saved stack pointer is
complete before any other worker can observe the task in the queue. A task
never requeues itself, which is what keeps a resumed stack from being written by
two workers at once.

### D3 - Cooperative scheduling: return or `yield ()`

A task runs until its entry returns or it calls `yield ()`. This is the
language contract (MASTER_SPEC 25) and it is enforced by the absence of anything
else: there is no timer, no signal and no preemption point. `yield ()` outside a
task is a no-op, so the same function can run on the main thread and in a task.

### D4 - Wait and wake on one epoch word

Workers block in `crug_pal_wait_u32` on a single scheduler epoch. Every state
change that can make a task runnable, or that can let a worker exit, bumps the
epoch, so a worker that observed an old value is woken and re-tests its
predicates. The queue length is never a wait predicate; that is what keeps the
shutdown path abort-simple (no lost-wake edge case between "queue empty" and
"shutdown set"). Spurious wakes are allowed and cheap.

### D5 - Spawn copies the frame; the compiler owns its shape

`crug_task_spawn (entry, frame, frame_size)` copies `frame_size` bytes out of
the caller's frame before returning (ADR 0008 D5). The compiler emits, per call
site, a struct with one field per argument (plus the function pointer for the
indirect form), a thunk that casts the frame pointer back and makes the call,
and one `crug_task_spawn` call (`docs/crug/GOROUTINES.md`). The runtime never
sees types, ownership or destructors; that is what makes the ownership checks
in the front end (CONC-011, CONC-012, CONC-014) the only place a capture is
judged.

### D6 - Failures are fatal at spawn, and shutdown is idempotent

`crug_task_spawn` has no error return: a task is guaranteed to be queued or the
process dies with a diagnostic (out of memory, or the PAL failed to start a
worker). The MVP has no cancellation and no partial-start state. Shutdown runs
every remaining task, joins the workers and releases the stacks; it is
idempotent, and a later spawn starts the runtime again. `spawned`, `completed`,
`yielded`, `lost` and `stack_corrupt` are lifetime counters, and `lost` is
`spawned - completed` at the last shutdown, which the stress gate requires to be
0.

### D7 - The runtime is absent from kernel and plain-C builds

The compiler emits a call to `crug_task_spawn` only for a `go` statement, and
`-fcrug-kernel` removes `-fcrug-goroutines` with a hard error. A program that
never writes `go` and `yield` never references the runtime: the kernel corpus
objects are checked for `__crug_` symbols and a libcrug dependency
(`scripts/check-kernel-objects.sh`, phase-2 evidence reused by the phase-14
gate).

## Consequences

- 100000 trivial tasks run on at most 64 workers; the stress run proves
  `tasks=100000 completed=100000 lost=0 corrupt=0 workers=4`, and `strace`
  counts 4 thread-creating calls for it, not 100000.
- A blocking call inside a task blocks a whole worker. That is acceptable for
  the MVP and is why the phase exposes no I/O; the poller phase revisits it.
- Because scheduling is cooperative, a task that never yields and never returns
  starves its worker, not the process. The contract says so.
- The global queue will not scale to many workers; a work-stealing queue is a
  later, measurable change (the spin lock is already isolated behind
  `queue_lock`/`queue_unlock`).

## Alternatives considered

- **One OS thread per task.** Rejected: it falsifies M:N (MASTER_SPEC 2.6) and
  the 100000-task acceptance number, and it costs a stack plus a kernel entry
  per task.
- **Work stealing now.** Rejected: no measurement justifies it, and the phase
  non-goals name it explicitly.
- **Preemptive scheduling with a timer.** Rejected by the phase non-goals; it
  would also make every task a potential data-race site without Send/Sync
  (phase 17).
- **A condition variable per task.** Rejected by D4: a state-predicate epoch is
  smaller, has no lost-wake edge case, and maps directly onto the PAL wait
  primitive.
- **Spawn returning an error.** Rejected for the MVP by D6: the source-level
  contract has no error channel for `go`, so a half-started task would be a
  silent state the compiler cannot express.
