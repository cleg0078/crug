# TASKS — the goroutine task runtime

Status: **v1 implemented** (2026-10-19, Phase 14; ADRs 0008, 0009, 0013). Task
groups arrived in Phase 15 (ADR 0024, `SCOPE.md`). The interface is
`libcrug/core/task.h`; the scheduler is `libcrug/core/task.c`; the context
switch is `libcrug/arch/x86_64/sysv/`.

```
Phase 14 exists to make one thing true before channels appear: M tasks run on
N operating-system workers through the PAL, and the compiler can lower a go
statement into a frame plus one spawn call. Phase 15 adds task groups on top:
a scope owns the tasks spawned inside it and waits for them.
```

## 1. The model

| Fact | Value |
|---|---|
| tasks per process | unbounded (100000 in the stress gate) |
| workers | `min (usable processors, 8)`, clamped 1..64; `CRUG_TASKS_WORKERS` |
| ready queue | one global FIFO, atomic spin lock, no stealing, no priorities |
| scheduling | cooperative: until the entry returns or `yield ()` |
| task stack | one private fixed stack, default 64 KiB; `CRUG_TASKS_STACK` |
| stack guard | one 4096-byte guard page at the low end, canary at the first committed word |
| preemption | none |
| cancellation | none |
| task ids | 1..N per process, 0 on the main thread |
| task groups | one per `scope` block; a group waits for its children (Phase 15) |

A goroutine is a stackful fiber, never an operating-system thread
(MASTER_SPEC section 25.1). The M:N ratio is the whole point: `strace` shows
four thread-creating calls for 100000 tasks (gate check 3).

## 2. Interface

The header is normative; this table is the map.

| Entry point | Contract |
|---|---|
| `void crug_task_spawn (crug_task_entry entry, const void *frame, size_t frame_size)` | copies `frame_size` bytes, queues the task, returns; no error return (a failure is fatal) |
| `void crug_task_yield (void)` | requeues the calling task behind the ready tasks and switches to another; no-op on the main thread |
| `void crug_task_runtime_shutdown (void)` | runs every remaining task, joins the workers, releases the stacks; idempotent and restartable |
| `void crug_task_stats_get (struct crug_task_stats *out)` | snapshot of the counters |
| `uint32_t crug_task_current_id (void)` | the calling task's id, 0 on the main thread |
| `void *crug_scope_enter (void)` | creates a task group; fatal on allocation failure, never NULL |
| `void crug_scope_leave (void *scope)` | waits for the group's children, then releases it; NULL is a no-op |
| `void crug_task_spawn_scoped (void *scope, ...)` | like `crug_task_spawn`, but the task joins the group |
| `void crug_scope_stats_get (struct crug_scope_stats *out)` | `entered`, `left`, `live` group counters |

`crug_task_entry` is `void (*) (void *)`: the entry receives a pointer to
the copied frame. The compiler generates that thunk; a program that calls the
runtime by hand must pass the address of its own frame struct.

## 3. Counters

`struct crug_task_stats` has `spawned`, `completed`, `yielded`,
`lost`, `stack_corrupt`, `workers`, `queue_peak` and `live`.
`spawned`, `completed` and `yielded` are **lifetime** counters: they keep
their value across a shutdown, so a test that shuts down and spawns again sees
the totals, not the second run. `lost` is `spawned - completed` at the last
shutdown; `live` is the current difference. Group counters are separate:
`struct crug_scope_stats` reports `entered`, `left` and `live`, where `live`
must be 0 once every scope has been left (the stress gate reads it).

## 4. Environment

| Variable | Meaning | Range |
|---|---|---|
| `CRUG_TASKS_WORKERS` | worker count for the process, read before the first spawn | 1..64 |
| `CRUG_TASKS_STACK` | bytes per task stack, read before the first spawn | 16 KiB..16 MiB |
| `GOROUTINES_STRESS` | makes `tasks-stress` run its 100000-task profile | any value |

An out-of-range value is fatal at runtime with a diagnostic naming the
variable, never silently clamped downward.

## 5. What the runtime may not do

`libcrug/core` touches the operating system only through
`<crug_pal.h>` (ADR 0011). `scripts/lint-no-os-apis.sh libcrug/core` must
report 0 references to `pthread_`, `epoll`, `futex`, `clone`,
`CreateThread`, `kqueue`, `WaitOnAddress`, `mmap`, `clock_gettime` and
the rest of the list; it is phase-14 gate check 6.

## 6. Limits, stated not discovered

- **No preemption.** A task that never yields and never returns starves its
  worker, not the process.
- **No cancellation.** Shutdown runs tasks, it does not kill them. A scope
  whose child never returns hangs in `crug_scope_leave`.
- **No growable stacks.** A task that needs more than `CRUG_TASKS_STACK`
  overflows into the guard page (or trips the canary where guard pages are
  declined).
- **No unwinding across a yield** and no `setjmp`/`longjmp` across a task
  boundary (ADR 0008 D6, ADR 0013 D4).
- **ThreadSanitizer cannot follow the switch.** Task programs are verified
  with ASan and UBSan; race freedom is a phase-17 (Send/Sync) question.
- **No I/O and no timers.** A blocking call blocks a worker. The poller is
  deferred past 1.0 (RFC-0001 section 32).
- **No kernel-mode runtime.** `-fcrug-kernel` removes `-fcrug-goroutines`
  with a hard error (MASTER_SPEC section 17.8).
- **A waiting scope is cooperative.** Inside a task `crug_scope_leave` yields
  in a loop; there is no parked WAITING state in v1 (ADR 0024 D3).

## 7. Evidence

| Check | Result |
|---|---|
| `make check-GOROUTINES` | `goroutines: 27 compiler test(s), 5 task program(s), 100000 tasks on 4 worker threads, 0 failure(s)` |
| `make check-SCOPE` | `scope: 23 compiler test(s), 8 group program(s), 0 leaked group(s), 0 failure(s)` |
| `GOROUTINES_STRESS=1 ./build/tasks-stress` | `tasks=100000 completed=100000 checksums=100000 lost=0 corrupt=0 workers=4 queue_peak=4` |
| `strace -f -e trace=clone,clone3 ... ./build/tasks-stress` | 4 matches with `CRUG_TASKS_WORKERS=4` |
| `make -C libcrug check-sanitizer` | ASan+UBSan clean |
| `scripts/check-kernel-objects.sh out` | `kernel-objects: 20 objects, __crug_=0, libcrug=0` |
| `scripts/lint-no-os-apis.sh libcrug/core` | `0 operating-system primitive references` |

The five task programs are `spawn`, `yield`, `capture`, `owned` and
`shutdown` under `libcrug/tests/goroutines/`; each prints a
`goroutines-*` line that the check script asserts. `tasks-stress` lives in
`libcrug/tests/` and is also built as `build/tasks-stress`.

## 8. Tests

| Target | Covers |
|---|---|
| `make -C libcrug PAL=fake check-PAL` | the PAL contract with no operating system |
| `make -C libcrug PAL=linux check-tasks` | the scheduler on the real backend |
| `make check-GOROUTINES` | the whole phase (DejaGnu compiler cases + runtime programs + the stress and `strace` counts) |
| `make check-GOROUTINES_DG` | only the DejaGnu cases under `gcc/testsuite/crug.dg/goroutines/` |
| `make -C libcrug check-sanitizer` | the task suite and the group API under ASan, UBSan and LeakSanitizer |
| `make check-SCOPE` | the structured-scope phase, including the group counters and the stress program (`SCOPE.md`) |
