# GOROUTINES — the go statement and yield

Status: **v1 implemented** (2026-10-19, Phase 14; ADRs 0008, 0009, 0013).
The structured `scope` statement is part of the same surface and is described
in `SCOPE.md` (Phase 15, ADR 0024). Channels and `select`, the way tasks pass
values, are described in `CHANNELS.md` (Phase 16, ADR 0025). Front end: `gcc/c/crug-tasks.cc`
(lowering) with the parser hooks in `gcc/c/c-parser.cc` and the capture checks
in `gcc/c/crug-ownership.cc`. Runtime: `docs/crug/TASKS.md`.

```
A go statement is the only way Crug code creates a task. Everything about it is
provable at the call site: the callee, its prototype, and where each captured
value goes.
```

## 1. Surface

Both forms are statements, with -fcrug-goroutines on:

```c
go worker (job, 7);        /* form A: a named function            */
go (table->fn (job));      /* form B: any call expression         */
yield ();                  /* let another task run                */
```

Grammar (ADR 0018 D4):

```
go-statement    ::= "go" call ";"
call            ::= identifier "(" [ argument-list ] ")"
                  | "(" postfix-expression ")"
yield-statement ::= "yield" "(" ")"
```

`go` and `yield` are contextual: a declared variable or function of that
name wins, exactly like `some`/`none` (ADR 0002). `go:` is still a label and
`int go = 11;` still declares a variable; `gcc/testsuite/crug.dg/goroutines/`
has a case for each.

Arity is checked against the callee's prototype; a mismatch is
`too few arguments to concurrent task` / `too many arguments to concurrent
task`.

## 2. What the compiler emits

For `go worker (a, b);` at one call site:

```c
struct __crug_task_frame_17 { typeof (a) a; typeof (b) b; };
static void crug_task_thunk_1 (void *__frame)
{
  struct __crug_task_frame_17 *f = __frame;
  worker (f->a, f->b);
  /* the runtime counts completion */
}
```

and the statement becomes, in a bind expression that owns the frame:

```c
struct __crug_task_frame_17 frame;
frame.a = a;
frame.b = b;
crug_task_spawn (crug_task_thunk_1, &frame, sizeof frame);
```

The frame is copied by `crug_task_spawn` before it returns, so the caller's
local `frame` dies immediately (ADR 0008 D5). The thunk is `static`,
artificial and named `crug_task_thunk_<N>`; a named function therefore shows
up in the assembly as one generated symbol per call site, and a program with no
`go` has no runtime reference at all.

Form B is the same with one extra field: the function pointer is captured as
field 0 and the thunk calls through it. The compiler cannot verify the callee
of an indirect form beyond its type, so the ownership checks in section 3 are
skipped for it (the runtime is type-blind by design).

The runtime interface is declared by the compiler itself, so a Crug translation
unit needs no header for `go`:

```c
void crug_task_spawn (void (*entry) (void *), const void *frame, size_t frame_size);
void crug_task_yield (void);
```

## 3. Ownership at the boundary

The front end knows the callee (form A), so it checks every captured argument
against the parameter it lands in. These are the Crug rules (MASTER_SPEC
section 27, ADR 0018 D4), all errors:

| Code | Situation | Text |
|---|---|---|
| `crug-CONC-011` | an owned value is captured without moving it | `owned value passed to concurrent task without ownership transfer` + `use move (...) to transfer it` |
| `crug-CONC-011` | `move` targets a parameter that is not `own` | `the task does not take ownership of the captured value: declare the parameter own` |
| `crug-CONC-012` | a `ref`/`mut` borrow (or `borrow()`/`borrow_mut()`) is captured | `a borrow cannot be captured by a concurrent task; capture an owned value with move (...)` |
| `crug-CONC-014` | `adopt (p, dtor)` is captured | `a destructor cannot cross a task boundary; the task destroys an owned parameter by itself` |

The rule behind them: a task outlives the statement that spawned it, so a
borrow could dangle and a destructor could run on the wrong stack. Ownership
moves through the frame, and the callee's `own` parameter is then destroyed by
the task itself (an `own` parameter is destroyed at the callee's scope end, so
the callee is the right stack for that destructor).

An owned parameter with no destructor must be released explicitly with
`release (p)`; `drop (p)` reports `crug-OWN-007` because the parameter has no
known destructor (see `docs/crug/OWNERSHIP.md`).

## 4. Callee requirements

| Requirement | Code |
|---|---|
| the callee is a function (named or through a pointer) | `crug-CONC-010` |
| it returns `void` | `crug-CONC-008` |
| it has a prototype (K&R and variadic functions are refused) | `crug-CONC-013` |

C23 makes `void f ();` a prototype, so only a K&R definition triggers
CONC-013.

## 5. yield

`yield ()` requeues the calling task behind the ready tasks and switches to
another. It is a no-op on the main thread, which lets the same function run
both ways. `yield` takes no arguments, is a statement, and is gated on the
same flag. Because scheduling is cooperative, `yield ()` is the only
preemption point in the language.

## 6. scope

`scope { ... }` is a task group: every `go` inside the block joins it and the
block waits for every child before it ends. It is a statement, gated on
`-fcrug-goroutines`, and it changes what a capture may borrow. The lowering,
the borrow rules, the runtime API and the limits are in `SCOPE.md`;
`gcc/testsuite/crug.dg/scope/` and `libcrug/tests/scope/` are the tests.

## 7. Flags

`-fcrug-goroutines` enables the surface and `__CRUG_GOROUTINES__`, and it is
part of `-fcrug-modern`. It does **not** imply `-fcrug-ownership`: `go` is
useful with only plain C values. Capturing an `own` value additionally needs
`-fcrug-ownership` (otherwise `own` is not a qualifier at all) and capturing a
borrow needs `-fcrug-borrow`, which is also when CONC-011 and CONC-012 can
fire. `-fcrug-kernel` **removes** it, and the combination is a hard error:

```
cc1: error: '-fcrug-kernel' removes '-fcrug-goroutines', '-fcrug-channels' and '-fcrug-modern'; give one or the other
```

Without the flag, `go f (x);` is not a task: form B reads as a call to an
undeclared function `go`, and form A is a declaration or an error, depending on
the tokens.

## 8. Tests

| Target | Covers |
|---|---|
| `make check-GOROUTINES_DG` | 17 DejaGnu cases under `gcc/testsuite/crug.dg/goroutines/`: two run forms, capture shapes, yield, owned capture, and the rejection cases for CONC-008/010/011/012/013/014 plus arity, flag-off, a variable named `go` and a `go:` label |
| `make check-GOROUTINES` | the DejaGnu cases, the five runtime programs and the stress/`strace` counts |
| `make check-SCOPE` | the structured-scope suite: 23 DejaGnu cases and eight group programs (`SCOPE.md`) |

The five runtime programs (`libcrug/tests/goroutines/`) print their own
`goroutines-*` line: `spawn` (8 tasks, sum=36), `yield` (4 yields),
`capture` (zero args, an array and a struct), `owned` (an owned buffer moved
into a task and released there) and `shutdown` (idempotent shutdown, restart).

## 9. Limits

- A task's captures are plain C values: no generic task type, no tuple, no
  closure over a local's address. A borrow is refused outside a scope
  (CONC-012); inside a `scope` block a borrow of a value that outlives the
  block may be captured (`SCOPE.md`, ADR 0024 D4).
- `go` cannot be used as an expression and there is no `go` with a result
  value; a task that must report a result uses a channel (`CHANNELS.md`) or an
  FFI callback.
- The runtime is absent from kernel and plain-C builds: a program with no
  `go`/ `yield` never references `__crug_`.
