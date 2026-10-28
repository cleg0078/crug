---
name: crug
description: "Write, port, build and debug Crug programs — GCC's opt-in extension that adds Rust-style ownership/borrowing (own, move, adopt, ref, mut, drop) and Go-style concurrency (go, scope, chan, send, recv, select) to C. Use when compiling with -fcrug-* flags, when a crug-UAM/OWN/DROP/BORROW/TYPE/CONC/ADT diagnostic fires, or when the user asks for Crug code, linking libcrug, or Crug performance tuning."
---

# Crug

Crug is an **opt-in extension of the GCC C front end** (GCC 16.2 here, Crug 0.2.3). With no `-fcrug-*` option the compiler is plain GCC: same macros, same parses, byte-identical object code. Every feature sits behind its own flag, and flags close over their prerequisites.

Three ideas, and nothing else:

| layer | what you get | flag |
|---|---|---|
| ownership & destruction | one owner per value, automatic destructor calls, no GC | `-fcrug-ownership` |
| borrowing | `ref`/`mut` checked aliasing, zero-cost, erased | `-fcrug-borrow` |
| tasks & channels | stackful goroutines, structured `scope` groups, typed channels | `-fcrug-goroutines` / `-fcrug-channels` |

There is no new runtime for ownership or borrowing — they are compile-time analyses that erase. Only tasks and channels link `libcrug`.

## 1. Pick the smallest flag set

| flag | enables | implies |
|---|---|---|
| `-fcrug-ownership` | `own`, `raw`; `move`, `drop`, `adopt`, `release`; `unsafe {}` | — |
| `-fcrug-borrow` | `ref`, `mut`; `borrow()`, `borrow_mut()` | ownership |
| `-fcrug-alias-facts` | `mut`/`own` pointer parameters become `restrict`; unproven call sites rejected | borrow |
| `-fcrug-slices` | `slice<T>`, `slice (ptr, len)`, checked `s[i]` | — |
| `-fcrug-result` | `option<T>`, `result<T,E>`, `some`, `none`, `ok`, `err`, `match` | `-fcrug-adt` |
| `-fcrug-adt` | payload enums and `match` | — |
| `-fcrug-goroutines` | `go`, `yield`, `scope`; Send/Sync capture checks | — |
| `-fcrug-channels` | `chan`, `channel`, `send`, `recv`, `close`, `select` | goroutines |
| `-fcrug-strict` | checking mode: every `-Wcrug-*` becomes an error, unchecked operations need `unsafe` | ownership + borrow |
| `-fcrug-ffi=<file>` / `-fcrug-ffi-discover` | C ownership contracts from `.crugffi` sidecars | ownership |
| `-fcrug-modern` | all eight feature flags (not kernel, not alias-facts) | all |
| `-fcrug-kernel` | plain GNU C kernel mode; excludes goroutines/channels/modern, rejects ownership/borrow | — |

Also: `-fcrug-dump=ownership,drop,borrow,alias` writes analysis dumps next to the object; `-fno-crug-bounds-checks` removes the `slice` bounds trap.

Feature macros exist only in Crug mode and are always defined as `1`: `__CRUG__`, `__CRUG_OWNERSHIP__`, `__CRUG_BORROW__`, `__CRUG_GOROUTINES__`, `__CRUG_CHANNELS__`, `__CRUG_STRICT__`, `__CRUG_SLICES__`, `__CRUG_RESULT__`, `__CRUG_ADT__`, `__CRUG_ALIAS_FACTS__`, `__CRUG_KERNEL__`, `__CRUG_FFI_VERSION__`. Guard optional features with `#ifdef __CRUG_CHANNELS__`.

## 2. Ownership: the one rule to internalise

`own T *p` means **exactly one owner**, no implicit copy, destroyed automatically when its scope ends. The qualifier goes *before* the type specifiers (`own struct node *p`, never `struct node *own p`).

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct buf
{
  char data[32];
  size_t len;
};

/* One destructor per owning type; it receives the owner. */
static void
buf_destroy (struct buf *b)
{
  free (b);
}

/* A shared borrow: many readers may hold one at a time. */
static size_t
buf_len (ref struct buf *b)
{
  return b->len;
}

/* An exclusive borrow: a writer must be alone. */
static void
buf_push (mut struct buf *b, char c)
{
  b->data[b->len++] = c;
  b->data[b->len] = 0;
}

/* An own parameter transfers ownership; it carries NO destructor, so the
   callee adopts it to get automatic destruction. */
static void
buf_report (own struct buf *b)
{
  own struct buf *mine = adopt (b, buf_destroy);

  printf ("report: %s\n", mine->data);
}

int
main (void)
{
  own struct buf *b = adopt (calloc (1, sizeof *b), buf_destroy);

  {
    mut struct buf *m = borrow_mut (b);
    buf_push (m, 'h');
    buf_push (m, 'i');
  }

  {
    ref struct buf *r = borrow (b);
    printf ("len=%zu text=%s\n", buf_len (r), r->data);
  }

  buf_report (move (b));
  return 0;
}
```

```
$ ./gcc/crugc -O2 -fcrug-borrow ownership.c -o ownership && ./ownership
len=2 text=hi
report: hi
```

What that program demonstrates, in the order it matters:

1. **Destructors are attached, not implied.** `adopt (p, dtor)` binds `void (*)(T *)`. Allocation results from `malloc`, `calloc`, `realloc`, `reallocarray`, `strdup`, `strndup` and `aligned_alloc` are already owned with `free` once `free` is declared, so `own char *s = strdup (x);` needs no `adopt`.
2. **Destruction happens on every exit path** — fallthrough, `return`, `break`, `continue`, `goto` — in reverse declaration order, as a null-guarded direct call. No drop flags, no helper, no refcount.
3. **`return` evaluates the value before running the drops**, so returning a pointer into an owned object is legal only when ownership does not move with it.
4. **`move (x)` transfers ownership**; afterwards `x` is poisoned (use = `crug-UAM-001`, moving again = `crug-UAM-002`, implicit copy = `crug-UAM-003`).
5. **An `own` parameter is owned by the callee but has no destructor.** Either `adopt` it (as above) or `release` it. Leaving it unhandled warns `crug-OWN-007`; `drop` on a parameter is `crug-OWN-007` too.
6. **`release (p)` abandons ownership without destroying; `drop (p)` destroys right now.** Both are rejected on non-owned values (`crug-DROP-001`).
7. **Owned values are not copyable.** Pass `move` to transfer, `ref`/`mut` to lend. An owning struct member is fine, an owning union member is `crug-OWN-009`.
8. **Branches must agree.** The analysis tracks EMPTY / OWNED / MOVED / MAYBE_MOVED / MAYBE_EMPTY; a join that disagrees becomes `MAYBE_*` and is rejected at destruction (`crug-DROP-006`, `crug-DROP-007`). Declare owners where every path leaves them in the same state.
9. **No unwinding.** `exit`, `abort` and fatal signals skip destructors; `setjmp`/`longjmp` only warn (`crug-DROP-003`) and are rejected under `-fcrug-strict`.
10. **Reassignment leaks.** Assigning a new value to a live owner is `crug-DROP-008` — wrap the old value in an inner block instead.

An `own` return type parses but is **erased at the boundary**, and the caller's ownership is not tracked from it (a documented deferred item). So callers write the ownership explicitly: `own struct buf *b = adopt (buf_new (), buf_destroy);`, or construct in the caller as above. Never rely on `own Foo *f (void)` to tell you that you own the result.

## 3. Borrowing: shared XOR exclusive

```c
ref struct buf *r = borrow (b);        /* shared: read only, many at once */
mut struct buf *m = borrow_mut (b);    /* exclusive: write, must be alone  */
```

- A borrow names a **whole owned object** — not `p->field`, not `a[i]`, not `*q` (`crug-BORROW-009`), not an owner that owns nothing (`crug-BORROW-011`).
- Lifetimes end at the **last read** (last-use), so the owner becomes usable again right after the last use of the borrow. Loops are treated conservatively (all borrows live); `goto` falls back to the lexical rule.
- While a shared borrow is live: no write through it (`crug-BORROW-008`), no `borrow_mut`, no move/release/drop/assign of the owner, and no read of the owner. While a mutable borrow is live: no second `borrow`/`borrow_mut`, no read of the owner.
- A borrow may cross a call **only** as a `ref`/`mut` argument (`crug-BORROW-020`); it cannot be returned (`019`), stored (`018`), or put in a struct/typedef.
- Passing a plain pointer where `ref T *` is expected is accepted by default; at a `mut T *` parameter it is accepted but proves nothing. `-fcrug-alias-facts` is the mode where exclusivity is enforced (`crug-BORROW-021/022/023`, plus `024` when disjointness cannot be proven) — use it when you want the optimizer's `restrict`, not by default.
- Everything erases: `ref T *` and `mut T *` have the representation of `T *`, and `borrow(e)` lowers to `e`. Zero cost, proven by the project's byte-identity checks.

Practical rule: borrow for parameters (`ref` = read, `mut` = write) and keep the owner at the call site. Do not try to make borrows a data structure; they are call-scoped views.
## 4. Tasks: `go`, `yield`, `scope`

`go f (args);` copies the arguments into a heap frame and queues a stackful fiber — the callee may return and keep locals; there is no continuation or state machine.

Requirements (`crug-CONC-*`): `f` must be a **function with a prototype returning `void`** (008/010/013), the arity must match, and every captured argument must be Send. Owned arguments must be moved at the capture (`go consume (move (p))`, `crug-CONC-011`); a borrow is only capturable when the lender outlives the scope block (`012`); arrays are rejected because they decay to a non-Send pointer (`024`). `go` implies nothing about ownership: the callee destroys its own parameter, so an `own` parameter without `adopt`/`release` leaks.

`scope { ... }` is a **structured task group**: every `go` in the block joins it, and leaving the block on any path waits for all of them. That is the only join mechanism; there are no task handles. Waiting inside a task yields; waiting on the main thread parks it in the PAL, so the wait never burns a worker.

`yield ();` gives up the worker after the current task. A task is **pinned to the first worker that runs it** and never migrates; the scheduler is cooperative — no preemption, no cancellation, no priorities, no work stealing. A CPU-bound loop that never yields or blocks starves its worker: insert `yield ();` every few thousand iterations, or split the work into more tasks than workers.

Runtime API (`task.h`): `crug_task_spawn`, `crug_task_yield`, `crug_task_runtime_shutdown` (runs every remaining task, joins the workers, releases the stacks; idempotent and restartable; fatal when called from a task, which uses the checked form instead), `crug_task_runtime_try_shutdown` (0 stopped, 1 nothing to stop, 2 called from a task, 3 join failure), `crug_task_stats_get`, `crug_task_current_id`, and the scope group calls `crug_scope_enter/leave`, `crug_task_spawn_scoped`, `crug_scope_stats_get`. Spawning during shutdown is fatal (`crug-CONC-029`); a frame too large for the copy is `crug-CONC-028`.

Stacks are **fixed**: 64 KiB default, one guard page, a canary. A task that recurses deeply or has big locals dies (or is reported by `stack_corrupt`); size `CRUG_TASKS_STACK` up instead. Since 0.2.3 the stack is materialised only when a worker dispatches the task (ADR 0008 A-3), so a spawn burst is cheap in both time and address space.

## 5. Channels and `select`

```c
chan struct job jobs = channel (0);            /* capacity 0: rendezvous handoff */
chan struct result results = channel (1000);   /* buffered: 1000 in flight      */

send (jobs, j);              /* completes when the receiver consumes it    */
struct job j = recv (jobs);  /* blocks while empty                         */
close (jobs);                /* idempotent; send after close is fatal      */
```

- `chan T` is an opaque handle (a pointer at runtime). Capacity is fixed at creation; `0` means a synchronous handoff. The channel object is **never freed** in v1 — `crug_chan_stats.live == created` is the normal state, not a leak. Do not `free` it.
- **Close + drain yields a zeroed element.** On a closed, empty channel `recv` returns zeroes (NULL for an owned pointer). Idiom: `if (j.id == 0) break;` for an id-shaped job, or `if (!p) break;` for an owned pointer. Sending after close is fatal.
- The element must be a **complete object type** (`crug-CONC-019`): give every struct a tag, because `typedef struct { ... } X;` has no complete type at the `chan` declaration. Not `void`, not a function, not incomplete.
- An owned element requires `send (ch, move (p))` (`crug-CONC-003`); an adopted element uses the adopt form (`023`). A `select` cannot receive from an owning-element channel (`017`) — use `recv`.
- `select` arms: `case v = recv (ch):`, `case recv (ch):`, `case send (ch, v):`, and exactly one `default:` which must be last. It is a rotation over non-blocking claims, so keep the send expression side-effect free and avoid `select` in the hottest loop.
- Introspection: `crug_chan_len`, `crug_chan_cap`, `crug_chan_stats_get` (created, live, sent, received, wait). `recv` lowers to `crug_chan_recv_into` / `crug_chan_recv_into_owned` (destination of element size, aligned; returns 1 for a value, 0 when closed and drained). The old pointer forms (`crug_chan_recv_ptr` / `_owned`) still exist for compatibility; their per-thread scratch is 16-byte aligned and can be retained by a detached external thread.

## 6. A complete verified program

```c
/* A worker pool: four tasks consume a channel of jobs, a closed channel
   terminates them, and the scope joins them all. */

#include <stdio.h>

#include "chan.h"
#include "task.h"

enum { JOB_COUNT = 1000, WORKERS = 4 };

struct job
{
  int id;
};

struct result
{
  long sum;
};

static long
work (int id)
{
  long s = 0;

  for (int i = 1; i <= id; i++)
    s += i;
  return s;
}

static void
worker (chan struct job jobs, chan struct result results)
{
  for (;;)
    {
      struct job j = recv (jobs);

      /* A closed and drained channel yields a zeroed element: id == 0. */
      if (j.id == 0)
	break;

      struct result r = { work (j.id) };
      send (results, r);
    }
}

int
main (void)
{
  chan struct job jobs = channel (0);			/* rendezvous handoff */
  chan struct result results = channel (JOB_COUNT);	/* buffered           */
  long total = 0;
  struct crug_task_stats tasks;
  struct crug_chan_stats chans;

  scope
    {
      for (int w = 0; w < WORKERS; w++)
	go worker (jobs, results);

      for (int i = 1; i <= JOB_COUNT; i++)
	{
	  struct job j = { i };
	  send (jobs, j);
	}
      close (jobs);

      for (int i = 0; i < JOB_COUNT; i++)
	{
	  struct result r = recv (results);
	  total += r.sum;
	}
    }

  crug_task_runtime_shutdown ();
  crug_task_stats_get (&tasks);
  crug_chan_stats_get (&chans);

  printf ("total = %ld\n", total);
  printf ("tasks: spawned=%llu completed=%llu lost=%llu workers=%u\n",
	  (unsigned long long) tasks.spawned,
	  (unsigned long long) tasks.completed,
	  (unsigned long long) tasks.lost, tasks.workers);
  printf ("channels: sent=%llu received=%llu waits=%llu\n",
	  (unsigned long long) chans.sent,
	  (unsigned long long) chans.received,
	  (unsigned long long) chans.wait);

  return total == 167167000 && tasks.lost == 0 ? 0 : 1;
}
```

```
$ make -C libcrug PAL=linux
$ ./gcc/crugc -O2 -fcrug-goroutines -fcrug-channels -Ilibcrug/core pool.c \
    libcrug/build/linux/task.o libcrug/build/linux/pal_linux.o \
    libcrug/build/linux/switch.o libcrug/build/linux/stack.o \
    libcrug/build/linux/chan.o -pthread -o pool
$ ./pool
total = 167167000
tasks: spawned=4 completed=4 lost=0 workers=8
channels: sent=2000 received=2000 waits=747437
```

Note the shape, because it is the one that does not deadlock: the result channel is **buffered** to the burst size, and the join happens at the end of the `scope` block. The classic mistakes are a capacity-0 result channel with the receive loop *after* `scope` (all workers block on send, the scope waits for the workers, deadlock) and spawning all work before receiving anything.

## 7. Build and link

`gcc/crugc` is a shell launcher over the in-tree `build/obj/gcc/xgcc`. It adds **no** includes, libraries or defaults of its own: whatever you need, you pass.

```
# 1. the runtime, only for tasks/channels
make -C libcrug PAL=linux
#    -> libcrug/build/linux/{task,chan,pal_contract,pal_linux,switch,stack}.o

# 2. compile + link
./gcc/crugc -O2 -fcrug-goroutines -fcrug-channels -Ilibcrug/core prog.c \
  libcrug/build/linux/task.o libcrug/build/linux/pal_linux.o \
  libcrug/build/linux/switch.o libcrug/build/linux/stack.o \
  libcrug/build/linux/chan.o -pthread -o build/prog
```

- Ownership/borrow/strict/FFI/safe types need **no runtime and no `-pthread`**: `./gcc/crugc -O2 -fcrug-modern prog.c -o build/prog` is enough (plus `-lm` for `math.h`).
- `crugc` compiles `.c` files. A `.crug` extension is not recognised as C — rename it.
- Missing runtime objects show up as implicit declarations of `crug_task_spawn`/`crug_chan_make`, not as linker errors: that means `-Ilibcrug/core` or the objects are missing.
- `PAL=fake` exists for tests but has no real concurrency (threads run to completion inside join) — never use it to run blocking code.
- `make examples` builds every `examples/*.c`; `examples/README.md` is the authoritative flag/command table per feature.

A minimal Makefile stanza:

```make
CRUG      := ./gcc/crugc
RT        := libcrug/build/linux
RT_OBJS   := $(RT)/task.o $(RT)/pal_linux.o $(RT)/switch.o $(RT)/stack.o $(RT)/chan.o
CFLAGS    := -O2 -g -Wall -Wextra -Ilibcrug/core

pool: pool.c $(RT_OBJS)
	$(CRUG) $(CFLAGS) -fcrug-goroutines -fcrug-channels $< $(RT_OBJS) -pthread -o $@
```

## 8. Runtime knobs

Set **before the first spawn**; the runtime reads them once. An out-of-range value is fatal, never clamped.

| variable | range | default | meaning |
|---|---|---|---|
| `CRUG_TASKS_WORKERS` | 1–64 | `min(usable CPUs, 8)` | worker threads |
| `CRUG_TASKS_STACK` | 16384–16777216 | 65536 | bytes per task stack |
| `CRUG_TASKS_CACHE` | 0–65536 | 64 | global cached stacks |
| `CRUG_TASKS_TASK_CACHE` | 0–65536 | 256 | cached task/frame allocations |

```
$ CRUG_TASKS_WORKERS=1 CRUG_TASKS_STACK=1048576 ./prog
```

Report them from the program with `crug_task_stats_get` (`workers` field), not from the environment, so a stat line proves the setting took effect.
## 9. Performance: what "use it well" means here

Spawn is cheap — about **100 ns** for a queued 64 KiB task — but the scheduler is a single global FIFO with a spin lock and a broadcast wake. It does not scale with workers for fine-grained tasks:

| shape | 1 worker | 8 workers |
|---|---|---|
| empty body, spawn + drain (per task) | 393 ns | 2061 ns |
| 480 000 pixel tasks, spawn all then drain | 0.149 s | 0.53–0.65 s |
| same, windowed to 8192 in flight | 0.184 s | 0.81–0.88 s |

The same program in Go (480 000 real goroutines) ran in 0.89 s with `GOMAXPROCS=8`; Crug spawns about 9× faster and is roughly comparable end-to-end at 8 workers, about 2× faster at 1.

Rules of thumb:

1. **Bound the in-flight count.** A spawn-all burst of N tasks reserves N task records and N stacks (only materialised at dispatch since 0.2.3, but still). Window it: spawn at most `cap` tasks, drain the results, repeat. `bench/raytracer/` in this repo shows both shapes side by side.
2. **Make tasks coarse.** Below a few microseconds of work per task the queue lock dominates. Compute a chunk per task (a row, a tile, a batch of items) rather than one item per task.
3. **Measure worker counts.** For CPU-bound work test 1, 2 and 8: more workers is not faster here. Workers pay off for *blocking* work (channel rendezvous, file/socket I/O) and for coarse independent jobs.
4. **Size result channels to the burst.** A capacity-0 channel is a rendezvous — good for handoff and backpressure, terrible if thousands of producers all block on it.
5. **`yield ();` in long CPU loops** — a task never migrates and never gets preempted, so an unyielding loop owns its worker.
6. **Stack size is per task.** Drop it for many small tasks, raise it for deep recursion; measure peak with `/proc/self/status` `VmPeak` or with program stats.
7. **`select` is a poll loop.** Fine for control paths; in a hot path prefer a plain `recv`.
8. **Zero-cost layers stay zero-cost.** Ownership, borrowing, `slice`/`option`/`result` and `match` lower to plain C; if a hot function looks slower, look at the runtime, not at the annotations.

## 10. Reading diagnostics

Every rejection carries a stable code `crug-<family>-<number>`; the registry is `docs/crug/DIAGNOSTICS.md`. Families you will meet:

| family | covers |
|---|---|
| `crug-UAM-*` | ownership state: use-after-move, double move, implicit copy |
| `crug-OWN-*` | `own` and `adopt`/destructor rules, ownership at call boundaries |
| `crug-DROP-*` | destructor placement: non-owned drop, maybe-moved, goto into scope |
| `crug-BORROW-*` | borrow shape, conflicts, escapes, last-use |
| `crug-TYPE-*` | qualifier placement and attachment (`own int`, `own T *a[4]`) |
| `crug-CONC-*` | `go` captures, channel elements, send/recv misuse |
| `crug-ADT-*` | `option`/`result`/`match`/payload-enum rules |
| `crug-SAFE-*` | strict mode: `raw` deref, int↔pointer, `asm`, `setjmp` |

Debugging options:

```
-fcrug-dump=ownership,drop,borrow,alias   # writes <stem>.crug-* next to the object
-fcrug-strict                             # promotes every -Wcrug-* warning to an error
```

The dumps are line-oriented ("borrow b10:28 a shares p", "end ... (last-use)") and are the fastest way to see *why* a move or a borrow was rejected. Warnings worth respecting: `crug-OWN-007` (unadopted `own` parameter — leaks) and `crug-DROP-003` (setjmp/longjmp escapes the analysis).

## 11. Pitfalls checklist

1. **Anonymous struct as a channel element** — `typedef struct { int x; } Item;` then `chan Item` is `crug-CONC-019`. Write `struct Item { int x; };` and `chan struct Item`.
2. **`own` parameter without `adopt`/`release`** — the callee owns it but no destructor travels with a pointer, so nothing frees it (`crug-OWN-007`; an error under strict).
3. **Borrowing a part** — `borrow (p->field)` is `crug-BORROW-009`. Borrow the owner, copy the field out, or take a plain pointer inside the callee.
4. **Keeping a borrow in a struct or returning it** — rejected by design (018/019). Restructure to pass `ref`/`mut` parameters.
5. **Assigning to a live owner** — `p = other;` is `crug-DROP-008`. Use an inner block so the old value dies at its end.
6. **Spawn-all then join** — memory grows with the burst and 8 workers get slower. Window the spawns and keep tasks coarse.
7. **Capacity-0 results plus join-before-drain** — deadlock: workers block on send, the scope waits for workers. Buffer the results or receive before the join.
8. **Moving a value twice** — `go f (p); go g (p);` needs one `move` and one other access; capture by `ref`/`mut` or move once.
9. **`send` after `close`** — fatal, not an error return. Close once, from the producer side, after the last send.
10. **Relying on `exit`/`abort` for cleanup** — destructors do not run. Flush with an explicit `drop` if it matters.
11. **`-Wall -Wextra` false positive** — channel send operands can trigger "set but not used" (`examples/18-channels.c`); the send still happens. Do not chase it.
12. **`slice` bounds trap** — out-of-range indexing is `__builtin_trap` → SIGILL / exit 132. That is the feature; `-fno-crug-bounds-checks` removes it.
13. **`match` is a statement** — `x = match (...)` does not parse yet; arms use `=>` with an optional comma, `_` must be last, and an owning payload enum must be scrutinee'd as `match (move (x))`.
14. **`.crug` file extension** — crugc only treats `.c` as C.
15. **Forgetting the runtime objects** — implicit declarations of `crug_task_spawn`/`crug_chan_make` mean the include path or the objects are missing, not that the flag is wrong.

## 12. Verify before claiming success

```sh
./gcc/crugc -O2 -Wall -Wextra -fcrug-modern -Ilibcrug/core prog.c ... -o prog   # clean, minus the known false positive
./prog                                                                          # runs
```

Then check the numbers the runtime gives you:

- `crug_task_stats_get`: `lost == 0`, `stack_corrupt == 0`, `completed == spawned`, and `workers` equal to what you intended.
- `crug_scope_stats_get`: `live == 0` (every group left).
- `crug_chan_stats_get`: `received == sent` for a drained pipeline; `live == created` is normal.
- `queue_peak` is a diagnostic, not an invariant: it depends on dispatch timing and is expected to be large for a spawn burst.
- Compare timing against the same program at `CRUG_TASKS_WORKERS=1` before blaming the compiler: the scheduler is the first suspect.

## 13. Where to read more

Inside the Crug repository:

- `docs/crug/LANGUAGE.md` — the feature set and what each flag gates (start here)
- `docs/crug/OWNERSHIP.md`, `DROP.md` — ownership states, destructor placement
- `docs/crug/BORROW.md`, `BORROWING.md` — borrow rules, last-use, alias facts
- `docs/crug/GOROUTINES.md`, `SCOPE.md`, `TASKS.md` — capture rules, groups, runtime, env knobs
- `docs/crug/CHANNELS.md`, `SEND-SYNC.md` — blocking semantics, select, Send/Sync
- `docs/crug/SAFE-TYPES.md` — `slice`/`option`/`result`/payload enums and `match`
- `docs/crug/STRICT.md`, `UNSAFE.md` — strict mode, `raw`, `unsafe`
- `docs/crug/DIAGNOSTICS.md` — every code, with its meaning
- `docs/crug/PERFORMANCE.md` — measured dispatch, channels, spawn bursts
- `examples/` + `examples/README.md` — the authoritative flag/command per feature
- `libcrug/core/task.h`, `libcrug/core/chan.h` — the runtime API in full
- `bench/raytracer/` — a real 480 000-task program with a stat section
