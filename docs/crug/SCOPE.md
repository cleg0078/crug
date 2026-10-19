# Structured scope

Status: v1 implemented 2026-10-19 (Phase 15). Decisions are recorded in
[ADR 0024](adr/0024-structured-scope.md); the runtime API is
[`libcrug/core/task.h`](../../libcrug/core/task.h).

A `scope` block is a task group: every `go` statement inside the block joins
that group, and the block does not complete until every child has returned.
That single rule is what makes a borrowed capture sound and what the
non-goals of this phase do not change.

## 1. The statement

```c
scope {
    go worker (a, b);
    go worker (c);
}
/* both children have finished here */
```

- `scope` is a contextual word, like `go` and `yield`. It starts a statement
  only as an undeclared identifier directly followed by `{`. A declared
  `scope` (`int scope = 11;`) and a `scope:` label keep their C meaning.
- A bare `scope;` is an ordinary expression statement over an undeclared
  identifier, so it is a C error, not a scope statement.
- The statement needs `-fcrug-goroutines`; without that flag `scope` is not
  special at all.
- Nesting is per block: an inner scope waits for its own children before the
  outer block continues, and the outer scope then waits for its own children.
- A `goto` into a scope body is rejected (`[crug-CONC-015]`); a `goto`
  out of one waits for the group first.

## 2. Lowering

```c
scope { go worker (a); }
```

lowers to roughly

```c
void *__scope_17;
__scope_17 = crug_scope_enter ();
{
  struct { typeof (a) a; } __frame;
  __frame.a = a;
  crug_task_spawn_scoped (__scope_17, crug_task_thunk_1, &__frame,
                          sizeof __frame);
}
crug_scope_leave (__scope_17);
```

The handle is an artificial variable declared in the block. It is what makes
the block a `BIND_EXPR`, so the ownership pass has a frame to attach the wait
to: the wait is a pseudo-destruction that runs **before** the block's own
destructions and is emitted on the normal end of the block and on `return`,
`break`, `continue` and `goto` out. The order matters when a child borrows an
owned local: the wait has to happen while the object is still alive.

The frame, thunk and spawn for the `go` statement itself are the Phase 14
machinery (see [GOROUTINES.md](GOROUTINES.md)); the only difference is that the
spawn names the group.

## 3. Borrowed captures

A child of a scope is provably finished before the block ends, so a borrow into
it is safe when the lender outlives the block:

```c
own int *n = adopt ((int *) malloc (sizeof (int)), free);

*n = 0;
scope {
    go bump (borrow_mut (n));
}
drop (n);                       /* the mut borrow is over here */
```

The rules the compiler enforces:

| Situation | Result |
|-----------|--------|
| `borrow(...)`/`borrow_mut(...)` captured inside a scope, lender at the scope body's depth or shallower | accepted |
| the same capture outside any scope | `[crug-CONC-012]` |
| the lender is declared in a block that ends before the scope does | `[crug-CONC-016]` |
| two children of one scope mutably borrow the same lender | `[crug-BORROW-003]` |
| one child borrows mutably while another borrows the same lender | `[crug-BORROW-002]` |
| an owned value captured without `move` | `[crug-CONC-011]` |
| `adopt(...)` inside a captured expression | `[crug-CONC-014]` |

The capture must name the borrow directly. A borrow copied into a local and
captured from there is not followed into the scope, so it is still rejected as
an unstructured capture.

## 4. Runtime API

```c
void *crug_scope_enter (void);
void  crug_scope_leave (void *scope);
void  crug_task_spawn_scoped (void *scope, crug_task_entry entry,
                              const void *frame, size_t frame_size);

struct crug_scope_stats { uint64_t entered, left, live; };
void crug_scope_stats_get (struct crug_scope_stats *out);
```

- The group object is opaque. `crug_scope_enter` never returns NULL: a group
  that cannot be created is a fatal runtime error, not a silent degradation.
- `crug_task_spawn_scoped` raises the pending count before the task can run,
  so the waiter never sees an empty group while a child is being built. A NULL
  handle means `crug_task_spawn`.
- `crug_scope_leave` waits until the count is zero and then releases the group.
  Inside a task it waits by `crug_task_yield()` (a worker must not block while
  its own children are queued); on the main thread it waits in the PAL with
  `crug_pal_wait_u32`. A NULL handle is a no-op.
- `crug_scope_stats_get` reports the lifetime counters. `live` must be 0
  after the last scope has been left; the stress program and the gate read it.

The runtime ABI version stays 1: the Phase 14 entry points keep their
signatures and the group API is additive.

## 5. Limits

- No cancellation and no error aggregation: a child that never returns hangs
  the scope; a task cannot return a value yet.
- No detached children, no task-local storage, no channels or select (Phase 16).
- The wait is cooperative. A waiting task consumes scheduler turns instead of
  being parked; a parked state is future work.
- A scope does not create an ownership boundary: an owned capture still uses
  `move`, and the callee still destroys what it owns.
- A scope inside a task and a scope on the main thread use different waits but
  the same guarantee.

## 6. Tests

| Command | What it proves |
|---------|----------------|
| `make check-SCOPE` | all of the below; prints `scope: 23 compiler test(s), 8 group program(s), 0 leaked group(s), 0 failure(s)` |
| `make check-SCOPE_DG` | `gcc/testsuite/crug.dg/scope/` runs under DejaGnu (14 fixtures, compile-only) |
| `scripts/check-scope.sh` | builds and runs `libcrug/tests/scope/*.c` and checks the group counters |
| `make -C libcrug check-sanitizer` | the group API under ASan, UBSan and LeakSanitizer (`libcrug/tests/scope_stress.c`) |
| `scripts/check-diagnostics.sh` | `CONC-015` and `CONC-016` are registered and cited |

The run cases under `libcrug/tests/scope/` and the line each prints:

| Program | Output |
|---------|--------|
| `wait.c` | `scope-wait: children=8 slots=8` |
| `borrow.c` | `scope-borrow: n=42` |
| `owned.c` | `scope-owned: seen=S frees=1` |
| `early.c` | `scope-early: early=1 marks=2` |
| `loop.c` | `scope-loop: last=2 marks=3` |
| `nested.c` | `scope-nested: after-inner=1 after-outer=2` |
| `inside-task.c` | `scope-in-task: inner=3` |
| `stress.c` | `scope-stress: tasks=1000 entered=200 left=200 leaked task group state: 0` |
