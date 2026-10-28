# Concurrency in Crug

Crug adds a cooperative concurrency surface: stackful tasks, task groups,
typed channels, `select` and the Send/Sync checks that guard a crossing.
Everything is opt-in; a translation unit compiled without the concurrency
flags is unaffected and the machine model stays C's.

Status: Phase 23 documentation of the Phase 14, 16 and 17 surfaces,
2026-10-20. Tasks are Phase 14 (ADR 0009), scopes Phase 15 (ADR 0024),
channels and `select` Phase 16, Send/Sync Phase 17 (ADR 0026). Canonical
pages: [TASKS.md](TASKS.md), [SCOPE.md](SCOPE.md), [CHANNELS.md](CHANNELS.md),
[SEND-SYNC.md](SEND-SYNC.md) and [PORTABILITY.md](PORTABILITY.md).

## 1. The concurrency surface

Two flags select the surface. `-fcrug-goroutines` enables `go` and
`scope`; `-fcrug-channels` enables `chan`, `send`, `recv`, `close`
and `select`. `-fcrug-modern` enables both, together with the other
modern features. There is no separate concurrency mode and no C thread
syntax: pthreads remain plain C and are not analysed.

| Surface | Form | Runtime entry |
| --- | --- | --- |
| task | `go f (args);` | `crug_task_spawn` |
| group | `scope { go f (args); }` | `crug_scope_enter`, `crug_task_spawn_scoped`, `crug_scope_leave` |
| channel | `chan T c = channel (n);` | `crug_chan_make` |
| send | `send (c, v);` | `crug_chan_send` or `crug_chan_send_owned` |
| receive | `T v = recv (c);` | `crug_chan_recv_ptr` or `crug_chan_recv_owned` |
| close | `close (c);` | `crug_chan_close` |
| select | `select { case ... }` | readiness calls, `crug_chan_poll_wait` |

A task is a stackful fiber, never an operating-system thread. The runtime
multiplexes many tasks over a small worker pool, so a program can create
100,000 tasks without creating 100,000 threads. The only preemption point is
a task entry returning or calling `yield ()`; there is no timer preemption.

## 2. Tasks and scheduling

`go f (a, b);` evaluates the arguments in the calling task, copies them
into a heap frame, and queues the frame with a compiler-generated thunk. The
lowering is `crug_task_spawn (thunk, &frame, sizeof frame);`. The callee
therefore receives by-value copies. An owned argument is moved into the
frame; after the `go` statement the caller no longer owns it. A raw pointer
argument is copied as an address and is not made safe by the copy.

Spawn never runs the callee inline. It either queues the task or terminates
the process with a diagnostic; `crug_task_spawn` has no error return. A
returning entry marks the task complete. The main thread has id 0; spawned
tasks receive ids 1 through N in creation order.

The worker count defaults to `min (usable processors, 8)`, clamped to 1
through 64, and can be set with `CRUG_TASKS_WORKERS` before the first spawn.
Each worker has its own queue for tasks that yielded, while never-run tasks
enter one global FIFO. There is no work stealing and no priority. A task is
pinned to the first worker that runs it; this is what lets the compiler cache
the address of a `_Thread_local` object in a callee-saved register
(ADR 0009 A-1). Per-worker queues also fixed the Windows and AArch64 failures
seen when two or more workers shared one queue.

A task owns one fixed stack, 64 KiB by default, rounded up to the platform
page size. `CRUG_TASKS_STACK` sets a size from 16 KiB to 16 MiB before the
first spawn. The runtime places one PAL guard page at the low end and a canary
at the first committed word. A stack never grows, never moves and is not
reused by another task; overflow hits the guard or the canary and is reported
as `stack_corrupt`. A task that needs more stack must use the heap or a
larger configured stack.

`yield ()` is a no-op on the main thread and a voluntary switch inside a
task. Unwinding across a yield is unsupported: a longjmp or a C++ exception
that crosses a task switch leaves the runtime inconsistent. ThreadSanitizer
cannot follow the custom context switch, so the task tests use their own
accounting rather than TSan.

`crug_task_runtime_shutdown ()` is idempotent: it runs every task to
completion, joins the workers and releases their stacks, and a destructor
runs it if `main` returns without calling it. A later spawn restarts the
runtime. `crug_task_stats_get` reports `spawned`, `completed`, `yielded`,
`lost`, `stack_corrupt`, `workers`, `queue_peak` and `live`;
`crug_task_current_id` returns the calling task's id.

## 3. Scopes

`scope { ... }` is a task group. The block creates a group on entry, every
`go` directly inside the block joins it, and leaving the block waits until
the group is empty. Scopes nest; each block has its own group and its own
pending count. The group handle is an ordinary opaque pointer held in the
block's `BIND_EXPR`, so it does not escape and cannot be forged in Crug.

The compiler lowers the block to `crug_scope_enter ()`, the body,
`crug_task_spawn_scoped (group, ...)` for each child, and
`crug_scope_leave (group)` on every path out. A `goto` into a scope is
`crug-CONC-015`; a `goto` out of a scope is accepted and performs the wait.
The wait is cooperative: on the main thread it waits in the PAL without
occupying a worker, and inside a task it yields.

Capture follows the ownership and borrow rules. An owned value captured by a
scoped `go` must be moved (`crug-CONC-011`); `adopt` of a borrowed value
cannot cross (`crug-CONC-014`). A borrowed capture is legal only when the
lender lives at the scope body's depth or shallower; a shorter-lived local is
`crug-CONC-012` by value and `crug-CONC-016` when the lifetime is not
provable. Two mutable captures of one place are `crug-BORROW-003`; a mutable
and a shared capture are `crug-BORROW-002`.

`crug_scope_stats_get` reports `entered`, `left` and `live`. After the
outermost scope has returned, `live` is zero. A scope does not collect
errors, return values or cancellation: a child that fails must report through
a channel or a shared structure chosen by the program. There is no detached
child, no task-local storage and no structured error propagation in v1.

## 4. Channels and select

`chan T` is a typed channel handle, represented as one opaque pointer. The
element type is compile-time only; the runtime sees an element size fixed by
`channel (n)` and copies values through `void *`. A channel's capacity is
fixed at creation. Capacity 0 is a synchronous handoff: at most one value is
in flight, a send waits for a receiver and a receive waits for a sender. A
capacity of N holds at most N values between sender and receiver.

`send (c, v)` blocks while the channel is full; `recv (c)` blocks while it
is empty. `close (c)` sets the closed flag and is idempotent. A receive
drains values that were already queued, then returns a zeroed element; for an
owned element that is a NULL pointer, which is the sentinel the ordinary
receive loop tests. A send on a closed channel is a fatal runtime error.
Blocked tasks yield to the scheduler, and a blocked main thread sleeps in the
PAL, so a full channel does not spin a worker.

`select` has four arm forms:

~~~c
select {
  case v = recv (a):  use (v); break;
  case recv (b):      drained (); break;
  case send (c, 7):   sent (); break;
  default:            idle (); break;
}
~~~

A `default` arm is optional, may appear once and must be last
(`crug-CONC-005`, `crug-CONC-006`). `case v == recv (a)` is a syntax error
(`crug-CONC-007`); the binding form is `case v = recv (a)`. When more than
one arm is ready, the runtime starts from a process-wide rotation counter, so
the same arm does not always win. With no ready arm and no `default`, the
task yields and retries.

An owned element type changes the checks. `chan own T` requires a `move`
on send (`crug-CONC-003`); `adopt` cannot cross the channel
(`crug-CONC-023`); a `select` cannot bind an owned receive
(`crug-CONC-017`). The element type and the sent value must be Send
(`crug-CONC-027`, `crug-CONC-024`), and a thread-affine value is refused
at the crossing (`crug-CONC-025`).

The runtime API is `crug_chan_make`, `crug_chan_send`,
`crug_chan_send_owned`, `crug_chan_recv_ptr`, `crug_chan_recv_owned`,
`crug_chan_recv_ready`, `crug_chan_send_ready`, `crug_chan_poll_wait`,
`crug_chan_select_turn`, `crug_chan_close`, `crug_chan_len`, `crug_chan_cap`
and `crug_chan_stats_get`. The ABI is version 1. A channel object is never
freed in v1: the registry keeps every channel reachable, so a receive after
close is well defined and a leak checker sees no lost block.

## 5. Send and Sync

Send and Sync are properties of a value, not qualifiers a program writes on
a normal declaration. They are inferred structurally, with an FFI sidecar
able to widen or restrict the inference. The checks run when
`-fcrug-goroutines` or `-fcrug-channels` is active; there is no
`-fcrug-send-sync` flag.

Send means the value may move to another task. Integers, floating types,
complex types, vectors, booleans, function pointers, channels, arrays whose
element is Send, and structures whose every field is Send are Send. An
`own T *` is Send when `T` is Send. A raw pointer, `void *`, an
incomplete type and a structure containing a raw pointer are not Send by
inference. Sync means a shared borrow of the value may cross to another task.
The base types are Sync; a shared scoped borrow of `p` requires the pointee
to be Sync, and a mutable scoped borrow requires the pointee to be Send.

| Crossing | Requirement | Diagnostic |
| --- | --- | --- |
| `go f (v)` | `v` is Send; an owned value is moved | `crug-CONC-024` |
| `go f (borrow (p))` in a scope | the pointee is Sync | `crug-CONC-026` |
| `go f (borrow_mut (p))` in a scope | the pointee is Send | `crug-CONC-024` |
| `send (c, v)` | element type and `v` are Send | `crug-CONC-027`, `crug-CONC-024` |
| `chan T c = channel (n)` | `T` is Send | `crug-CONC-027` |

A `.crugffi` declaration can state `send`, `!send`, `sync` or `!sync`
on a type, and `thread_affinity NAME` on a function. The annotation is a
review claim about the C library, not a compiler proof; a wrong `send` is as
serious as a wrong `consumes`. Thread affinity follows the value through
`move`, `borrow`, `borrow_mut` and `&`; any crossing is
`crug-CONC-025`, and the only v1 escape is to keep the value on its thread.
An `unsafe` block or a cast does not silence the analysis: a raw pointer
stays `!Send` until a sidecar says otherwise.

## 6. Runtime and ports

The runtime lives in `libcrug/core/task.c`, `libcrug/core/task.h`,
`libcrug/core/chan.c` and `libcrug/core/chan.h`, with context switches
under `libcrug/arch/` and operating-system services behind
`libcrug/pal/`. The core has no `#ifdef` on the host OS. `task.c` owns
the scheduler, stacks, scopes and the shutdown; `chan.c` owns the channel
registry, the ring, blocking and the select readiness calls. The runtime has
no external dependency beyond the C library and the PAL.

The supported hosts are Linux x86_64 (native), Linux AArch64 (cross-compiled
and tested under QEMU, plus native CI), Windows x86_64 under MinGW and Wine,
and macOS ARM64 in CI. macOS x86_64 builds but is not gated; the fake PAL is
a contract-only port. There is no BSD, Windows ARM64 or RISC-V port in v1,
and no AArch64 compiler cross-build: the runtime is tested on AArch64, but
the Crug compiler itself is not. `make check-MULTI_OS` drives the supported
hosts, and the CI workflow is `.github/workflows/multi-os.yml`.

The context switch is hand-written assembly. The SysV AMD64 switch saves
`rbx`, `rbp`, `r12` through `r15` and the MXCSR control and x87 control
words; the Microsoft x64 switch adds `rdi`, `rsi` and `xmm6` through
`xmm15`; the AAPCS64 switch saves `x19` through `x28`, `x29`, `x30`,
`d8` through `d15` and `FPCR`. The Windows TEB stack bounds are not
swapped, which is safe only because the runtime never grows a task stack and
the scheduler calls no TEB API. Darwin uses hashed mutex and condition
wait-wake and loops on spurious wakeups.

## 7. Limits

- No preemption and no priorities. A task runs until it returns or yields.
- No cancellation. A task cannot be stopped from outside, and a scope cannot
  abandon a child.
- No timeout on a send, receive, `select` or scope wait in v1.
- No detached child, no task-local storage and no error aggregation.
- No task stack growth, copying or reuse. The size is fixed before spawn.
- No I/O poller, timers or `io_uring` integration in v1. A blocked
  operation outside a channel still blocks its worker.
- No kernel-mode task runtime: under `-fcrug-kernel` the concurrency
  features are removed, and a kernel task backend is not in v1.
- No BSD, Windows ARM64 or RISC-V port, and no growable stacks, in v1.
- No `select` timeout, nil channel or priority send. A later release may
  add them; the v1 ABI has no fields reserved for them.

## 8. Tests

The compiler tests live under `gcc/testsuite/crug.dg/` and the runtime
programs under `libcrug/tests/`. Each suite has a wrapper script and a
`make` target that prints one summary line.

| Check | Expected result |
| --- | --- |
| `make check-GOROUTINES` | `goroutines: 31 compiler test(s), 5 task program(s), 100000 tasks on 4 worker threads, 0 failure(s)` |
| `make check-SCOPE` | `scope: 23 compiler test(s), 8 group program(s), 0 leaked group(s), 0 failure(s)` |
| `make check-CHANNELS` | `channels: 49 compiler test(s), 8 channel program(s), 0 failure(s)` |
| `make check-SEND_SYNC` | `send-sync: 14 compiler test(s), 0 failure(s); raw pointer rejected, thread affinity enforced, FFI annotations enforced` |
| `make check-MULTI_OS` | Linux x86_64 and the cross targets pass their jobs |

`GOROUTINES_STRESS=1` raises the task count in the stress program; the
default run already creates 100,000 tasks on four workers. The scope tests
assert that `crug_scope_stats_get` reports `live == 0` after the last
group and that a scope waits on every exit path. The channel tests cover the
ring, capacity 0, close-drain, a send on a closed channel, the four select
arms, the rotation and each ownership diagnostic. The Send/Sync suite covers
the crossing table, the raw-pointer rejection and the FFI override.

The gate entries are in [EXIT-GATES.md](EXIT-GATES.md) phases 14, 15, 16 and
17, with the archived output under `docs/crug/gates/`.

## 9. Post-rc1 changes (2026-10-26)

The runtime surface is unchanged in version: the Crug runtime ABI stays 1, and
the new entry points are additive. A capacity-0 send completes when the
receiver consumes its ticket rather than when the shared slot is empty
(CHANNELS.md section 7); the select claim pair replaces the polling probe; task
receive storage is per-task. Shutdown has a checked form returning 0/1/2/3 and
a spawn during shutdown is fatal (TASKS.md section 9, `crug-CONC-029`); frame
size arithmetic is checked (`crug-CONC-028`). The PAL contract moves to 1.2
because `crug_pal_thread_join` returns `int` (ADR 0011 A-2). Re-baselined
counts measured on 2026-10-28: goroutines 31 compiler cases + 5 programs,
channels 44 + 7, tasks 17 checks (125 completed), runtime Linux 15 programs
+ 2 expected-fatal programs, and PAL 13/0 on Linux. On the fake backend the
same lists are seven programs and two expected-fatal programs. The audit's
two-sender defect behind this rewrite is R4-RT-001.

Staleness note (ADR 0025 D5 amendment): a select send arm's value expression
is evaluated on every claim pass and again in the closed-send abort path, so it
must be side-effect-free apart from the move; a select arm that sends an owned
value is not transferred, which can produce a conservative `OWN-007` or
`DROP-006`.
