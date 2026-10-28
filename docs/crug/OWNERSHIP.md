# Crug ownership MVP: `own` and `move`

Status: **Phase 4 implemented, extended by Phase 5** (2026-10-19). `own` and
`move` are accepted under `-fcrug-ownership`, the analysis rejects the MVP
misuse classes, and the post-join state is dumpable. Phase 5 added destructors
and automatic destruction; that half of the model is described in
[DROP.md](DROP.md), and the states below are the expanded set it uses. Borrow
checking does not exist yet. The normative text is ADR 0003 (metadata storage),
ADR 0004 (analysis layer), ADR 0005 (drop lowering), ADR 0018 D2/D3/D9 (surface
grammar and diagnostics) and RFC-0001 sections 13-19; the gates are
`EXIT-GATES.md` phases 4 and 5.

## 1. What is accepted

| Form | Meaning | Notes |
| --- | --- | --- |
| `own T *p;` | `p` owns one `T` object | declaration specifier, erased from the type |
| `own T **p;` | `p` owns one pointer-sized object | the outermost pointer level owns |
| `own T a[4];` | four owned `T` objects, dropped 3..0 later | array declarator: the element type owns |
| `own` struct member | the containing object owns the member | dropped with the aggregate (Phase 5) |
| `own` parameter | erased at the call boundary | ADR 0003 D3; not tracked inside the caller |
| `own` return type | erased at the boundary | same |
| `typedef T *P; own P p;` | `p` owns one `T` | typedef cannot carry the qualifier |
| `move (e);` | transfers the owning value | `e` is an owning lvalue or owned prvalue |

The qualifier is a declaration specifier, so `own int i;`, `own T (*fp)(void);`,
a bit-field, a flexible array member and an unnamed member are rejected
(`crug-TYPE-001`, `crug-TYPE-006`). An owning member in a union is rejected when
the union is completed (`crug-OWN-009`, ADR 0004 D6): the MVP has no
active-variant contract, so a union of owning values has no defined destruction.

`move` is not a keyword and not a runtime helper. It is the identifier `move` in
callee position with no visible declaration and the feature on
(`__builtin_crug_move` internally). A user declaration named `move` wins, which
keeps every existing C program valid. The call is replaced by its operand, so
the object code contains a register copy and no call.

## 2. The state machine

Each tracked object is in one of five states:

| State | Meaning |
| --- | --- |
| `EMPTY` | owns nothing (uninitialized, or explicitly null) |
| `OWNED` | holds the responsibility to destroy the object; the destructor is a separate field |
| `MOVED` | transferred away; using it is an error |
| `MAYBE_MOVED` | the branches disagree; using it is an error |
| `MAYBE_EMPTY` | one path initialized it, another did not; destroying it is an error |

The tracked set for the MVP is every declaration whose specifiers carry
`own`, at variable granularity. Assigning an owned value (an `own`
declaration with an initializer, `move (p)`, `adopt (p, d)`) makes the target
`OWNED` and records the destructor; `move` makes the source `MOVED`.

At a control-flow join the analysis merges the incoming states. Equal states
stay; `EMPTY` with `MOVED` stays `EMPTY`; `OWNED` with `EMPTY` becomes
`MAYBE_EMPTY`; `OWNED` with `MOVED` becomes `MAYBE_MOVED`; a disagreement about
the destructor is `crug-OWN-010`. ADR 0003 D2 is explicit that the join must
not pick a side: an object that is owned on one path and moved on another has no
static answer, and the MVP rejects using it rather than guessing. The full table
and the destruction points are in [DROP.md](DROP.md) section 2.

The states are static facts stored in the frontend analysis, never in the object
or in a side table (ADR 0003 D1, D5). No hidden field, bit, reference count,
registry or helper call is added, so `sizeof`, alignment, layout and calling
representation are identical to plain C. The phase-4 gate checks that identity
twice: `abi-ptr.c` asserts the sizes and the object carries no `__crug_`
symbol, and `move-zero-cost.c` disassembles to zero calls.

## 3. Diagnostics

The ownership diagnostics are errors with no option spelling: `error_at` has
no option parameter, so they print `error: <message> [crug-AREA-NNN]` without a
`[-Wcrug-...]` annotation. Phase 5 added the warning layer for the diagnostics
that must not break existing code; those carry both an option and a stable code
(`-Wcrug-unadopted-own`, `-Wcrug-adopt-signature`, `-Wcrug-jump-into-scope`,
`-Wcrug-setjmp-drops`, `-Wcrug-drop-unreachable`), default on, and
`-fcrug-strict` promotes them to errors. Every message carries a stable code
from `docs/crug/DIAGNOSTICS.md`.

| Code | Trigger |
| --- | --- |
| `crug-UAM-001` | use of a moved value |
| `crug-UAM-002` | move of an already-moved value |
| `crug-UAM-003` | implicit copy of an owned value (owned values are not copyable) |
| `crug-UAM-004` | use of a possibly-moved value after a branch join |
| `crug-UAM-006` | move of an aggregate field is not supported in the MVP |
| `crug-OWN-001` | move operand is not an owned object |
| `crug-OWN-002` | `adopt` destructor has the wrong signature (warning) |
| `crug-OWN-004` | `release` operand is not an owned object |
| `crug-OWN-007` | owned value never adopted or released (warning) |
| `crug-OWN-009` | owning member in a union |
| `crug-OWN-010` | conflicting destructors for one object on different paths |
| `crug-DROP-001` | `drop` operand is not an owned object |
| `crug-DROP-002` | `goto` jumps into the scope of an owned object |
| `crug-DROP-003` | destructors may be skipped by `longjmp` (warning) |
| `crug-DROP-006` | cannot prove an object is destroyed exactly once on this path |
| `crug-DROP-007` | owned value may be used before it is initialized |
| `crug-DROP-008` | assignment to an owned value that already owns |
| `crug-TYPE-001` | qualifier on a declarator that owns nothing (for example `int`) |
| `crug-TYPE-002` | more than one Crug qualifier on one declarator |
| `crug-TYPE-004` | `_Atomic` combined with a Crug qualifier |
| `crug-TYPE-005` | owning function pointer type |
| `crug-TYPE-006` | qualifier on a bit-field, flexible array member or unnamed member |

The message format is the one the gate greps:
`file:line:col: error: <message> [crug-AREA-NNN]`. The area-code registry and
`scripts/check-diagnostics.sh` make the codes unrenumberable.

## 4. The ownership dump

`-fcrug-dump=ownership` writes `<stem>.crug-ownership` next to the object file
(the dump is named after the output base, so it follows `-o`). The first line is
`crug ownership dump v1`; each join prints one line:

`join <file>:<line>: <decl>=<A>/<B> -> <RESULT>`

For a variable owned on one branch and moved on the other:

`join branch-join.c:16: p=OWNED/MOVED -> MAYBE_MOVED`

The phase-4 gate compiles `branch-join.c` with the dump on and greps that line.
`-fcrug-dump=` takes a comma-separated list; the other kinds are `drop`, which
writes `<stem>.crug-drop` and is described in [DROP.md](DROP.md) section 6, and
`borrow`, which writes `<stem>.crug-borrow` and is described in
[BORROW.md](BORROW.md) section 4. An unknown kind is rejected (`unrecognized
argument to -fcrug-dump=`), and the option changes no object code.

## 5. What is deliberately absent

| Missing | Phase |
| --- | --- |
| ownership transfer at call sites: a callee does not check what a caller does with its owning parameters, and an owning return value is not tracked by the caller | later |
| borrow checking beyond whole objects ([BORROW.md](BORROW.md)): members, array elements, mutable reborrow, interior mutability | 12 (place-level borrows); Phase 7 added last-use lifetimes and the depth-zero call policy |
| partial moves (`move (s.field)`); rejected with `crug-UAM-006` | post-MVP, ADR 0004 D4 |
| termination on `exit`, `abort` or a signal (no destructor runs, documented in DROP.md section 5) | intentional |
| `setjmp`/`longjmp` destructor elision (warned with `crug-DROP-003`, not repaired) | post-MVP |
| kernel-mode ownership (`-fcrug-kernel` rejects `-fcrug-ownership`; kernel code carries no ownership analysis or drop lowering in v1, ADR 0017) | v1 non-goal; a future increment needs a superseding ADR |
| `-Wcrug-ffi-ownership` and the FFI-side warnings | Phase 9 |

Consequences worth knowing: an owning aggregate is non-copyable, so
`own struct Foo b = *a;` is rejected; the way to transfer is `move`; and an
owning object that is neither moved nor adopted is destroyed at the end of its
scope (Phase 5), with `-Wcrug-unadopted-own` warning when no destructor is known.

## 6. Tests

| Check | What it proves |
| --- | --- |
| `make check-OWNERSHIP` / `gcc/testsuite/crug.dg/crug.exp` | 17 expected passes over the `ownership/` directory |
| `abi-ptr.c` | `sizeof` / `_Alignof` identity and no `__crug_` symbol |
| `move-zero-cost.c` | `move` lowers to a register copy, no helper call |
| `use-after-move.c`, `double-move.c`, `implicit-copy.c` | the three rejection classes, each as a `dg-error` |
| `maybe-moved.c` | a join of owned and moved rejects the later use (`crug-UAM-004`) |
| `field-move.c` | partial moves are rejected (`crug-UAM-006`) |
| `own-union.c`, `own-flexible-array.c` | union and flexible-array member rejections |
| `branch-join.c` | the dump prints `p=OWNED/MOVED -> MAYBE_MOVED` |

Phase 5 extended the same suite (`make check-OWNERSHIP` is still 17 passes) and
added `make check-DROP` (39 passes) plus the `scripts/check-drop-*.sh` checks;
[DROP.md](DROP.md) section 7 lists them. The phase-4 gate runs the dg suite, the
two ABI checks, the diagnostic-code check and the dump check;
`scripts/gate.sh --run phase-4` archives the output.

## 7. Adding a tracked place

The pass lives in `gcc/c/crug-ownership.cc` and is called once per function
definition from `c_parser_declaration_or_fndef`. A new tracked place needs:
`crug_mark_owning_decl` at the declaration site, a case in `walk_expr` for the
statement that assigns or moves it, and a `dg-error` test in
`gcc/testsuite/crug.dg/ownership/`. Any change to the state lattice or to the
join rule is an ADR 0004 amendment, not a local edit.

## 8. Post-rc1: empty owned values (2026-10-26)

`crug-UAM-008` is the read of an empty owned value ("it holds nothing"); the
move of an empty owned value uses the same code. Rejected: comparison,
dereference, passing by value, returning, and taking the address of an empty
owner. Accepted: an assignment that initializes a fresh owned value, and
`release` or `drop` on an empty owner (destroying nothing is defined), and
passing `&empty` to an FFI `out_owned` argument. The state machine and the
join rule are unchanged; the check is a read/write distinction in the pass, not
a new state.

## 9. Post-rc1: explicit drop clears the variable (2026-10-28)

An explicit `drop (x)` on an owned pointer variable runs the destructor and
then stores NULL in the variable; it does not only mark the variable moved. The
guarded destruction at the end of the scope reads NULL and destroys nothing, so
an object dropped on one path is destroyed exactly once whichever path runs. A
drop inside a conditional (`if (c) drop (p);`) and a drop inside a loop are
accepted with no `crug-DROP-006`. A later use of the variable is still a
moved-value diagnostic: `crug-UAM-001` after an unconditional drop and
`crug-UAM-004` after a conditional one. A conditional expression that drops on
one arm and then drops again outside it (`c ? drop (p) : 0; drop (p);`) is
still rejected, now as `crug-DROP-006` at the declaration with a note at the
second drop, because the second drop's state depends on the arm.

The explicit destruction is emitted as `if (p) dtor (p); p = 0;`: the
destructor call is guarded, so a drop site that executes more than once (for
example one inside a loop) calls the destructor only while the variable is
non-NULL. A destructor supplied through `adopt` is therefore not invoked with
NULL by this lowering.
