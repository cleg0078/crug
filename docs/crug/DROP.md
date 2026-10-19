# CRUG drop lowering (automatic destruction)

Status: **implemented for Phase 5** (2026-10-19). This document is the
normative digest of ADR 0005 (drop lowering, Accepted 2026-10-18) as
implemented, together with ADR 0003 (ownership metadata) and ADR 0004
(aggregate ownership). Where they disagree, the ADR wins.

## 1. What this phase implements

With `-fcrug-ownership`, every owning object is destroyed exactly once on
every path that leaves its scope, by a direct call to its destructor and
nothing else:

- no runtime library, no hidden flag word, no `__crug_` helper call;
- no destructor metadata in the object file: the destructor choice is a
  compile-time fact (ADR 0003 D4);
- the owned pointer itself is an ordinary C pointer; the object layout and
  the calling convention are the C ones (ADR 0006).

The analysis runs in the C front end after parsing, on the GENERIC body, in
`crug_check_function` (gcc/c/crug-ownership.cc). It has two passes:

- **pass A** walks the body, tracks the ownership state of every owning
  place, records every exit edge together with the list of objects it must
  destroy, and writes the dumps;
- **pass B** rewrites the body: exit edges get their destruction statements,
  and the `move`, `adopt`, `release` and `drop` intrinsics are removed so
  that nothing reaches the middle end.

## 2. The ownership state

One state per tracked place, at variable (or parameter) granularity:

| state | meaning | destroyed at scope end |
| --- | --- | --- |
| `EMPTY` | owns nothing (uninitialized, or explicitly null) | no |
| `OWNED` | owns one value; the destructor is recorded separately (or unknown) | yes, if the destructor is known |
| `MOVED` | was moved out; owns nothing | no |
| `MAYBE_MOVED` | one incoming path moved it, another did not | an error: it cannot be proven |
| `MAYBE_EMPTY` | one incoming path initialized it, another left it empty | an error |

The merge at a control-flow join is total:

| incoming | result |
| --- | --- |
| equal states | that state (same destructor) |
| `OWNED` + `OWNED` with different destructors | `crug-OWN-010` |
| `EMPTY` + `MOVED` | `EMPTY` |
| `OWNED` + `EMPTY` | `MAYBE_EMPTY` |
| `OWNED` + `MOVED` | `MAYBE_MOVED` |
| anything + `MAYBE_MOVED` or `MAYBE_EMPTY` | `MAYBE_MOVED` / `MAYBE_EMPTY` |

A destruction point that sees `MAYBE_MOVED` or `MAYBE_EMPTY` is rejected
(`crug-DROP-006`, `crug-DROP-007`), not compiled with a runtime test: the
language promise is that the compiler *knows*, and a runtime flag would be
hidden state that ADR 0003 D5 forbids.

## 3. Where a destructor comes from

1. **`adopt (pointer, destructor)`** attaches an explicit destructor. The
   destructor must have type `void (*)(T *)` for the operand type `T`
   (`void *` is accepted); anything else is `crug-OWN-002`, and the value stays
   owned with no destructor, so `-Wcrug-unadopted-own` also fires where it would
   leak. A null destructor takes the same path: it is not a function designator,
   so it is diagnosed and attaches nothing.
2. **The allocation catalogue.** A call to `malloc`, `calloc`, `realloc`,
   `reallocarray`, `strdup`, `strndup` or `aligned_alloc` (including their
   `__builtin_` spellings) produces an owned pointer whose destructor is
   `free`, provided `free` is declared in the translation unit. If `free` is
   not declared, the value is owned with an unknown destructor and the
   warning below applies.
3. **Movement.** `move (p)` transfers the value and its destructor to the new
   owner; the source becomes `MOVED` and is not destroyed.
4. **Parameters.** An owning parameter (`own T *p` in a definition) is owned
   by the callee (ADR 0005 amendment A-1). It is destroyed on every callee
   exit, after the local objects. Call sites are not yet checked: passing an
   owning pointer to a function does **not** transfer ownership, so the caller
   still destroys it. A function that takes ownership must document its
   parameter as `own`, and callers must `move` into it once call-site checking
   exists.

A value that is still owned at the end of its scope with an *unknown*
destructor is reported by `-Wcrug-unadopted-own` (on by default, an error
under `-fcrug-strict`):

`owned value 'p' is never adopted or released [crug-OWN-007]`

The compiler does not guess: a pointer that came from an arbitrary function
is not silently freed with `free`.

## 4. Where destruction happens

Every exit edge recorded by pass A gets a destruction statement:

| exit kind | dump name | example |
| --- | --- | --- |
| end of a block | `scope-end` | fallthrough out of `{ ... }` |
| `return` | `return` | `return x;` |
| `break` | `break` | leaves the loop or switch |
| `continue` | `continue` | leaves the iteration |
| `goto` | `goto` | jumps out of one or more scopes |

Each destruction is emitted as `if (p) dtor (p);` where `dtor` receives the
pointer value (for `own Foo *p`, `dtor (p)`; for an `own` parameter the same
rule applies). Destruction order is the reverse of declaration order.

For `return expr;` the expression is evaluated **before** the destructions:
the front end materializes it in an artificial variable, destroys, and
returns that variable. This matters when the expression reads the value of
an object that is about to be destroyed.

An explicit `drop (p)` destroys at that point and leaves `p` in `MOVED`; a
second `drop` is a use of a moved value (`crug-UAM-001`). `release (p)`
relinquishes ownership without destroying and also leaves `p` `MOVED`.

## 5. Rejections this phase adds

| code | when |
| --- | --- |
| `crug-DROP-001` | `drop` of something that is not an owning object |
| `crug-DROP-002` | `goto` jumps into the scope of an owning object |
| `crug-DROP-003` | `setjmp`/`longjmp` may skip destructors (`-Wcrug-setjmp-drops`; error under strict) |
| `crug-DROP-006` | a path reaches the destruction point in `MAYBE_MOVED` state |
| `crug-DROP-007` | a path reaches the destruction point in `MAYBE_EMPTY` state |
| `crug-DROP-008` | assignment to an owned object that already owns |
| `crug-OWN-010` | two paths leave different destructors for one object |
| `crug-OWN-002` | `adopt` destructor has the wrong signature |
| `crug-OWN-004` | `release` operand is not an owned object |

`crug-DROP-003` is a policy warning, not a guarantee: the compiler does not
model the `longjmp` target set, so it says so at the `setjmp` call. This
matches RFC-0001 section 19 ("MUST NOT claim deterministic cleanup through
abnormal control flow it does not model").

## 6. Dumps

`-fcrug-dump=drop` writes one file next to the object (named after the
output file, `<base>.crug-drop`) with a header and one line per destruction:

```
crug drop dump v1
drop scope-end.c:30: scope-end p via my_free
drop early-return.c:24: return p via my_free
```

The line is `drop <file>:<line>: <exit-kind> <name> via <destructor|none>`.
`-fcrug-dump=` takes a comma-separated list; `ownership` prints the branch
joins (Phase 4) and `drop` prints this one.

## 7. Options and diagnostics

`-fcrug-ownership` enables everything in this document. `-fcrug-dump=` is the
only dump switch. The six warnings are declared in `gcc/c-family/c.opt`, all on
by default (`Init(1)`), and `-fcrug-strict` promotes them to errors.
`-Wcrug-drop-unreachable` has its option but no diagnostic yet: unreachable
destruction is not detected in this phase.

| option | code(s) |
| --- | --- |
| `-Wcrug-intrinsic-operand` | `crug-OWN-001`, `crug-OWN-004`, `crug-DROP-001` |
| `-Wcrug-adopt-signature` | `crug-OWN-002` |
| `-Wcrug-unadopted-own` | `crug-OWN-007` |
| `-Wcrug-jump-into-scope` | `crug-DROP-002` |
| `-Wcrug-setjmp-drops` | `crug-DROP-003` |
| `-Wcrug-drop-unreachable` | `crug-DROP-005` (reserved) |

Codes that are always errors carry no `-Wcrug-` annotation in the message:
`crug-UAM-001` ... `crug-UAM-006`, `crug-OWN-003`, `crug-OWN-005`,
`crug-OWN-008`, `crug-OWN-009`, `crug-OWN-010`, `crug-DROP-006`,
`crug-DROP-007`, `crug-DROP-008`.

## 8. Tests

- gcc/testsuite/crug.dg/drop/ (run by `make check-DROP`): 9 `dg-do run`
  programs counting allocations and destructions, 10 diagnostic tests, one
  dump test.
- gcc/testsuite/crug.dg/drop/exactly-once/: four programs with their own
  counting allocator (no libc), including the moved and released cases.
- gcc/testsuite/crug.dg/drop/asan/: three programs whose leaked or
  double-freed objects would show up under AddressSanitizer.
- Scripts: `scripts/check-drop-suite.sh`, `check-drop-exactly-once.sh`,
  `check-drop-asan.sh`, `check-drop-objdump.sh`, `check-drop-policy.sh`,
  `check-drop-dump.sh`.

## 9. Exit gate (Phase 5)

| check | command | observable |
| --- | --- | --- |
| 1 | `scripts/check-drop-suite.sh` | `drop-suite: 39 tests, 0 failures` |
| 2 | `scripts/check-drop-exactly-once.sh` | `exactly-once: 4/4 programs` |
| 3 | `scripts/check-drop-asan.sh` | `drop-asan: 3/3 programs, 0 with reports` |
| 4 | `scripts/check-drop-objdump.sh` | `drop-objdump: 5 objects, 0 with __crug_ calls` |
| 5 | `scripts/check-drop-policy.sh` | the four stable codes |
| 6 | `scripts/check-drop-dump.sh` | `drop-dump: scope-end return break continue goto 5 kinds` |

## 10. Non-goals and known limits

- No borrowing (`ref`/`mut`, Phase 6) and no borrow-aware drop.
- No partial moves: moving a field is rejected (ADR 0004); moving a whole
  aggregate is not tracked yet either, because aggregates are not owning
  places in this phase.
- No unions containing owning members (`crug-OWN-009`).
- Arrays of owning pointers are tracked as one place; per-element destruction
  is a later refinement.
- Call sites are not checked: a function that returns an owning pointer
  (`own T *f (void)`) is recorded, but its result is not treated as owned yet,
  and passing an owned pointer does not transfer ownership.
- No interprocedural analysis, no drop elision, no NLL.
- `setjmp`/`longjmp`: warning only.
- `-fcrug-kernel` cannot be combined with `-fcrug-ownership`: kernel-mode
  drop lowering is Phase 19.
- ASAN objects are not instrumented; the check catches leaks and invalid
  frees through the runtime interceptors.
- Exit-code-based tests rely on the driver finding the just-built compiler;
  use `make check-*` or set `CRUGC`.
