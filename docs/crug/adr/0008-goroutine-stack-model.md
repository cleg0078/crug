# ADR 0008 - Goroutine stack model: fixed private stacks with a guard page

- **Status:** Accepted
- **Date:** 2026-10-19
- **Deciders:** project owner (fixed stacks and the "no growable stacks" non-goal),
  runtime maintainer (the canary and the guard-page policy)
- **Relates:** ADR 0009 (scheduler model), ADR 0011 (PAL contract), ADR 0013
  (arch context switch), MASTER_SPEC sections 25.2-25.4 and 26.3, RFC-0001
  sections 35-37, CRUG_PRODUCTION_ROADMAP section 19,
  `docs/crug/TASKS.md`

## Context

MASTER_SPEC section 25 requires a goroutine to be a stackful fiber whose stack
comes from the PAL, with a guard page where the backend supports one, a fixed
initial size that is configurable, and explicitly **no** growable stacks.
Section 79 requires this ADR by name. RFC-0001 section 35 forbids the runtime
from naming an operating-system primitive, so `mmap` and friends cannot appear
in this decision; the PAL memory operations are the only tools available.

The phase-14 non-goals repeat the constraint: no growable stacks, no split
stacks, no preemption. What the specification does not say is how a stack is
laid out, how a stack overflow is detected, what happens to a corrupted stack,
and how the sanitizers see a fiber stack.

## Decision

### D1 - One private, fixed-size stack per task, from PAL memory

`crug_task_spawn` allocates one stack per task with
`crug_pal_memory_reserve` + `crug_pal_memory_commit` and releases it with
`crug_pal_memory_release` when the task finishes. The default is 64 KiB
(`CRUG_TASK_STACK_DEFAULT`); `CRUG_TASKS_STACK` overrides it for the process
before the first spawn, and the accepted range is 16 KiB to 16 MiB. The stack is
never grown, never copied and never moved: a task's local variables have one
address for their whole life, which is what lets an owned local live in a task
without the runtime knowing about ownership.

### D2 - The guard page is at the low end, and declining it is not an error

The reservation is `guard + stack` bytes: a guard page at the low address,
then the committed stack (see A-1: the guard is one PAL page, not always
4096 bytes). A downward-growing stack runs off the bottom
into the guard, which the Linux backend maps `PROT_NONE`; the process dies with
`SIGSEGV` instead of corrupting the neighbouring task. `crug_pal_memory_protect_guard`
is a capability: the fake backend declines it and the contract suite reports a
skip. Declining degrades detection to the canary check (D3); it is not a
runtime error, because a backend without virtual memory (a kernel port) still
has to run the core.

### D3 - A canary and a magic word make corruption countable

The lowest committed word holds a canary and the task header holds a magic
value. `crug_task_release` checks both and increments `stack_corrupt` instead
of returning the stack to service. A corrupted stack is therefore a counted
number in the stats, not silent memory corruption; the stress gate requires the
counter to be 0 over 100000 tasks.

### D4 - A stack is not reused while the MVP lives

There is no stack pool: the reservation is released when the task completes.
That is the simplest lifetime that satisfies "no reuse of a corrupted stack",
and 64 KiB per live task is an acceptable MVP cost. A pool is a later
optimisation that has to keep the canary contract, not a design requirement.

### D5 - The frame is copied into the task allocation

`crug_task_spawn (entry, frame, frame_size)` copies the compiler-built frame
into the task allocation (after the header) before it returns, so the spawning
function's stack frame may die immediately. This is why the compiler lowering
can put the frame in the caller's local scope without a lifetime problem
(ADR 0009 D5, `docs/crug/GOROUTINES.md`).

### D6 - Unwinding never crosses a switch; sanitizers are supported

A task's stack is a normal C stack, but it is only entered and left through
`crug_ctx_switch` (ADR 0013). `setjmp`/`longjmp` across a yield, C++-style
unwinding out of a task entry, and debugger stack walks that expect one thread
stack are outside the contract: the task boundary is opaque to unwinding. The
flip side is that ASan and UBSan work unmodified, because the switch saves and
restores only the callee-saved registers the ABI already requires (ADR 0013 D2);
`make -C libcrug check-sanitizer` runs the whole task suite under ASan and
UBSan and is a phase-14 gate check.

## Consequences

- A task that needs more than `CRUG_TASKS_STACK` bytes must move the data to
  the heap or the process must raise the limit; there is no automatic growth,
  and the failure is a guard-page fault or a counted corruption, never silent.
- One 64 KiB reservation per live task is the memory cost of a stackful fiber.
- The guard page is verified on Linux only (`tests/linux/pal_guard_fault.c`
  proves that the guarded byte faults); the fake backend skips it visibly.
- `stack_corrupt` being 0 over the stress run is evidence for the gate, not a
  proof: it proves that the stacks the runtime handed out came back intact.

## Alternatives considered

- **Growable or split stacks.** Rejected by the phase non-goals and by
  MASTER_SPEC 25.4: growth needs a stack-copy or a stack-splitting prologue,
  both of which change the ABI contract the compiler emits.
- **`malloc` for a stack.** Rejected by ADR 0011 D1: the core may not name an
  OS primitive, and a guard page needs virtual-memory operations.
- **Reuse finished stacks from a pool.** Rejected for the MVP by D4: the canary
  check exists to catch corruption, and a pool makes "corrupted stack returns to
  service" a design question before there is a measurement that needs the pool.
- **Detect overflow with a compiler-inserted stack probe.** Rejected: it costs
  code in every function for a case the guard page already covers.

## Amendments

### A-1 (2026-10-20): the guard is one PAL page, not always 4096 bytes

D2 assumed a 4096-byte page. The macOS port showed why that assumption cannot
live in the scheduler: Apple silicon uses 16 KiB pages, so `reserve + 4096` is
not page-aligned and `mprotect` (or `VirtualProtect`) refuses the range; every
task spawn aborted with "cannot commit a task stack". PAL v1.1 added
`crug_pal_page_size` (ADR 0011 A-1), and the scheduler now takes the guard as
`max (crug_pal_page_size (), 4096)` and rounds the stack size up to it before
reserving `guard + stack`. D2's "4096-byte guard page" therefore reads "one
PAL guard page"; the canary, the no-growth rule and the decline-is-not-an-error
rule are unchanged.

### A-2 (2026-10-27): bounded stack and task allocation caches

D4's no-pool rule is superseded by a bounded cache. `crug_task_spawn` now takes
a stack slot from a per-thread cache, and a completed task returns its slot to
that cache; a small global cache feeds threads whose local cache is empty. Task
headers and frames are served from global size-class bins. The defaults
are 64 stack slots in the shared cache (plus up to 32 local slots per thread) and
256 cached tasks per size class; both are
bounded and both are disabled by setting `CRUG_TASKS_CACHE=0` or
`CRUG_TASKS_TASK_CACHE=0` before the first spawn.

The canary contract is unchanged: a slot is reused only after its canary and
task magic have been checked, and a slot whose canary is wrong is released, not
returned to the cache. `stack_corrupt` still counts every such event. All
caches are flushed after the workers join in `crug_task_runtime_try_shutdown`,
so a restart starts with an empty cache and no live stack or task is freed by
the flush.

This changes no public ABI: `crug_task_spawn`, `crug_task_stats` and the task
execution model stay as they were. The 64 KiB default stack and the
`CRUG_TASKS_STACK` range are unchanged; the cache only removes the per-spawn
reserve/commit/malloc path.
