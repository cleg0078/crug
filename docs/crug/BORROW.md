# CRUG Borrows

Status: Phase 6 implemented and extended by Phase 7 (last-use liveness and the
interprocedural policy), 2026-10-19. This file describes the borrow checker as
it behaves today. ADR 0018 D2 and D3 remain canonical for the surface syntax;
ADR 0003 remains canonical for what a borrow is (static analysis metadata, no
object representation); ADR 0020 is canonical for when a borrow ends and ADR
0019 for what may cross a call.

## 1. What the checker implements

1. Two qualifiers, `ref` and `mut`, recognised as declaration
   specifiers exactly like `own` (ADR 0018 D2, amendment A-3: they must
   precede the type specifiers).
2. Two intrinsics, `borrow(e)` and `borrow_mut(e)`, recognised in
   call position when no user declaration of the identifier is visible.
3. A borrow checker: one mutable borrow or many shared borrows, never both.
4. Last-use lifetimes (Phase 7, ADR 0020): a borrow ends after the last
   statement that reads it, so the owner can then be moved, dropped, released
   or mutably borrowed. Joins union, loops are conservative, a `goto` in the
   body falls back to the lexical rule.
5. A depth-zero interprocedural policy (Phase 7, ADR 0019): a borrow crosses a
   call only as an argument to a `ref`/`mut` parameter; returning one, storing
   one in memory, or passing one to a plain parameter is rejected.
6. Rejections for every conflicting operation while a borrow is live.
7. A static borrow dump, `-fcrug-dump=borrow`, which prints the lender of
   every borrow and where it ends, with the reason when it ends at last use.

Nothing is emitted for a borrow. `ref T *` and `mut T *` have the
size, alignment and calling representation of `T *`; `borrow(e)`
lowers to `e`. `scripts/check-borrow-cost.sh` proves this by
compiling the same function twice, once with borrows and once with plain
pointers, and comparing the objects byte for byte.

## 2. The rules

### 2.1 What a borrow may name

A borrow names a whole object, never a part of one:

- `borrow(p)` requires `p` to be an owned lvalue (a variable, a
  parameter, or an existing shared borrow).
- `borrow_mut(p)` requires `p` to be an owned modifiable lvalue
  that is not itself a borrow.
- Members (`p->field`, `s.field`), array elements, dereferences and
  ordinary (non-owned) pointers are rejected with crug-BORROW-009.
- A non-lvalue operand is crug-BORROW-006.
- Borrowing an owner that currently owns nothing (`own T *p = 0;`) is
  crug-BORROW-011.
- A mutable borrow cannot be made from a shared borrow (crug-BORROW-012), and a
  mutable borrow cannot be borrowed again (crug-BORROW-010).

### 2.2 Conflicts

While a borrow is live:

| operation | shared borrows live | mutable borrow live |
|---|---|---|
| another shared `borrow` | allowed | crug-BORROW-016 |
| `borrow_mut` | crug-BORROW-002 | crug-BORROW-003 |
| write through the borrow | crug-BORROW-008 | allowed |
| `move` the owner | crug-BORROW-004 | crug-BORROW-004 |
| `release` the owner | crug-BORROW-004 | crug-BORROW-004 |
| `drop` the owner | crug-BORROW-005 | crug-BORROW-005 |
| assign to the owner | crug-BORROW-014 | crug-BORROW-014 |
| read the owner | allowed | crug-BORROW-015 |

Automatic drop at scope end is exempt: the destructor runs after the borrows it
ends, so the drop insertion of Phase 5 is not rejected by a borrow that dies
first.

### 2.3 Lifetime

A borrow begins at its declaration and ends after the last statement that reads
it (ADR 0020). The set of borrows live at a statement is computed by a backward
pass over the structured body:

    in(S)  = use(S) | (out(S) - def(S))
    out(S) = in(next statements), unioned at every join

The conservative rules are part of the decision:

- A join unions the two arms, so a borrow read in either arm is live after it.
- Inside a loop, every borrow of the function is treated as live (no fixpoint),
  but the loop's live-out set is not widened.
- A `goto` in the body makes liveness fall back to the lexical rule.

Escapes are unchanged: a borrow declared in an inner block cannot be stored into
a binding declared outside it (crug-BORROW-007), and no assignment can extend a
borrow beyond the block of the referent.

### 2.4 Borrows as parameters

A parameter declared `ref T *p` or `mut T *p` is a live borrow
inside the callee for the whole body, with an unknown lender. The callee can
re-borrow a `ref` parameter (`ref T *q = borrow (p);`) but cannot
move, drop or free it.

Call sites are checked at depth zero (ADR 0019): a borrow may be passed only to
a `ref`/`mut` parameter (crug-BORROW-020), a borrow may not be returned
(crug-BORROW-019) or stored in memory or a static (crug-BORROW-018), and no
function is summarized, so a call can never extend a borrow's lifetime. Passing
a plain pointer where a `ref T *` is expected remains accepted: the callee
then has a borrow the caller never granted, and Phase 8 owns that direction.
A plain pointer at a `mut T *` parameter is also accepted by default, but it
proves no exclusivity; `-fcrug-alias-facts` (ADR 0010) is the mode in which the
contract is checked: it rejects a plain pointer (crug-BORROW-021) and a shared
borrow (crug-BORROW-022) at a `mut` parameter, two borrows of one lender in
one call when at least one is mutable (crug-BORROW-002 and crug-BORROW-003), and
a call that passes a lender beside its own mutable borrow (crug-BORROW-023).

### 2.5 Where a borrow may appear

| position | `ref` / `mut` |
|---|---|
| local variable | yes |
| function parameter | yes (a live borrow in the callee) |
| function return type | no, crug-BORROW-001 |
| struct or union member | no, crug-BORROW-007 |
| typedef | no, crug-BORROW-007 |
| non-pointer declarator | no, crug-TYPE-007 |
| combined with `own` | no, crug-TYPE-002 |

## 3. Diagnostics

Every rejection carries a stable code (docs/crug/DIAGNOSTICS.md). The codes
implemented in this phase:

| code | meaning |
|---|---|
| crug-BORROW-001 | a function cannot return a borrow |
| crug-BORROW-002 | cannot mutably borrow while shared-borrowed |
| crug-BORROW-003 | cannot mutably borrow more than once |
| crug-BORROW-004 | cannot move or release while borrowed |
| crug-BORROW-005 | cannot drop while borrowed |
| crug-BORROW-006 | borrow operand must be an lvalue |
| crug-BORROW-007 | borrow escapes the lifetime of its referent |
| crug-BORROW-008 | assignment through a shared borrow |
| crug-BORROW-009 | operand must be a whole owned object or a borrow |
| crug-BORROW-010 | a mutable borrow of a borrow, or of a non-owned object |
| crug-BORROW-011 | cannot borrow an owner that owns nothing |
| crug-BORROW-012 | mutable borrow from a shared borrow |
| crug-BORROW-013 | borrow used without being initialized |
| crug-BORROW-014 | cannot assign to an owner while it is borrowed |
| crug-BORROW-015 | cannot read an owner while it is mutably borrowed |
| crug-BORROW-016 | cannot borrow while it is mutably borrowed |
| crug-BORROW-017 | borrow must be initialized from borrow(), borrow_mut() or another borrow |
| crug-BORROW-018 | a borrow must be an automatic variable, or cannot be stored in memory |
| crug-BORROW-019 | a borrow cannot be returned from the function that created it |
| crug-BORROW-020 | a borrow may only be passed to a ref or mut parameter |

## 4. Dump

`-fcrug-dump=borrow` writes `<output-base>.crug-borrow` next to the
object (the flag takes a comma-separated list, so
`-fcrug-dump=ownership,drop,borrow` works):

```
crug borrow dump v1
borrow b10-last-use-shared.c:28: a shares p
end b10-last-use-shared.c:29: a shares p (last-use)
borrow b10-last-use-shared.c:31: m excludes p
end b10-last-use-shared.c:35: m excludes p
```

The first word of a line is the record kind (`borrow`, `end`); the
name is the borrow binding and `shares`/`excludes` is the access it
grants to the lender. The dump is diagnostic output, not an input to code
generation.

## 5. What Phase 6 does not do

These are deferrals, not oversights. Each names the phase that owns it.

- **No call-site checking.** Passing an owner to a function that takes
  `ref T *` is not checked against the caller's live borrows, and an
  `own` argument is not moved. Phase 8 (cross-function ownership) owns the
  transfer rules; borrows follow them.
- **No place-level borrows.** Reborrowing a borrow (shared or mutable) is
  rejected (crug-BORROW-010, crug-BORROW-016); a later phase owns place-level
  borrows and with them reborrowing (Phase 12 is the migration tool).
- **No struct fields.** A borrow cannot be stored in a structure, so a borrow
  cannot outlive its function by being returned inside one either
  (crug-BORROW-018).
- **No interprocedural summaries.** Depth is zero by decision (ADR 0019), so
  helper functions cannot return borrows and borrows cannot reach libc until
  the FFI contracts of Phase 9.
- **No interior-mutability story.** `_Atomic` and `volatile` are
  neither borrowed nor excluded; a later phase decides how a shared borrow sees
  an atomic.
- **Kernel mode.** `-fcrug-kernel` still rejects `-fcrug-borrow`
  (ADR 0005 amendment A-7); Phase 19 revisits it together with `own`.

## 6. Tests and gate

`gcc/testsuite/crug.dg/borrow/` holds 46 fixtures run by
`make check-BORROW` (DejaGnu, `borrow.exp`): 88 tests at the Phase 7 commit,
13 programs that must run, 4 that must compile and dump or size-check, and 29
that must be rejected with a specific code. The six programs under
`real-c/` are compiled and run by a script, not by the dg suite.

| check | script |
|---|---|
| the dg suite | `scripts/check-borrow-suite.sh` |
| realistic programs | `scripts/check-borrow-real-c.sh` |
| escape rejection and origin note | `scripts/check-borrow-escape.sh` |
| last-use and scope ends in the dump | `scripts/check-borrow-nll.sh` |
| zero cost | `scripts/check-borrow-cost.sh` |
| ABI identity | `scripts/check-borrow-abi.sh` |
| stable codes | `scripts/check-borrow-policy.sh` |
| dump shape | `scripts/check-borrow-dump.sh` |
| registry consistency | `scripts/check-diagnostics.sh` |

The exit gate for Phase 6 is in docs/crug/EXIT-GATES.md (amendment A-7); the
Phase 7 gate is in the same document (amendment A-8).

## 7. Post-rc1: joins and proven disjointness (2026-10-26)

Two rules close the audited holes. First, the borrow state is joined like the
move state: when two predecessors end with different lender sets, the join keeps
a lender live only if both sides agree, and a borrow that is live on one path and
released on the other is rejected at the join instead of being silently dropped.
Second, `crug-BORROW-024` replaces the old optimistic credit: a `mut`
parameter, and any alias fact, needs a proven disjoint root. The code is
rejected when the compiler cannot prove disjointness, when an indirect call
passes pointer arguments (the facts stay unproven), or when a `mut` lender is
not unique. A borrow of an empty owned value remains accepted at an FFI
`out_owned` argument; `borrow()` of an empty owner is not a read.
