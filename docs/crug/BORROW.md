# CRUG Borrows

Status: Phase 6 implemented, 2026-10-19. This file describes the borrow
checker as it behaves today. ADR 0018 D2 and D3 remain canonical for the
surface syntax; ADR 0003 remains canonical for what a borrow is (static
analysis metadata, no object representation).

## 1. What Phase 6 implements

1. Two qualifiers, `ref` and `mut`, recognised as declaration
   specifiers exactly like `own` (ADR 0018 D2, amendment A-3: they must
   precede the type specifiers).
2. Two intrinsics, `borrow(e)` and `borrow_mut(e)`, recognised in
   call position when no user declaration of the identifier is visible.
3. A lexical borrow checker: one mutable borrow or many shared borrows, never
   both; a borrow cannot outlive the block of the object it borrows.
4. Rejections for every conflicting operation while a borrow is live.
5. A static borrow dump, `-fcrug-dump=borrow`, which prints the lender of
   every borrow and where it ends.

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

The lifetime is the lexical block of the lender. A borrow declared in an inner
block cannot be stored into a binding declared outside it (crug-BORROW-007), and
no assignment can extend a borrow beyond its block. This is the
non-lexical-lifetime rule reduced to the MVP: when the end of a borrow cannot be
proven, the borrow stays live until the end of its block.

### 2.4 Borrows as parameters

A parameter declared `ref T *p` or `mut T *p` is a live borrow
inside the callee for the whole body, with an unknown lender. The callee can
re-borrow a `ref` parameter (`ref T *q = borrow (p);`) but cannot
move, drop or free it. Call sites are not checked in this phase: passing a plain
pointer where a `ref T *` is expected is accepted, and the caller's own
borrow obligations are not enforced around the call. That gap is recorded in
section 5.

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

## 4. Dump

`-fcrug-dump=borrow` writes `<output-base>.crug-borrow` next to the
object (the flag takes a comma-separated list, so
`-fcrug-dump=ownership,drop,borrow` works):

```
crug borrow dump v1
borrow b01-multiple-shared.c:27: a shares p
borrow b01-multiple-shared.c:28: b shares p
end b01-multiple-shared.c:24: a shares p
end b01-multiple-shared.c:24: b shares p
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
- **No mutable reborrow.** `borrow_mut` of a borrow is rejected
  (crug-BORROW-010) rather than checked. Reborrowing is a Phase 7 decision.
- **No struct fields.** A borrow cannot be stored in a structure, so a borrow
  cannot outlive its function by being returned inside one either.
- **No NLL.** A borrow dies at the end of its block, not at its last use.
- **No interior-mutability story.** `_Atomic` and `volatile` are
  neither borrowed nor excluded; Phase 12 decides how a shared borrow sees an
  atomic.
- **Kernel mode.** `-fcrug-kernel` still rejects `-fcrug-borrow`
  (ADR 0005 amendment A-7); Phase 19 revisits it together with `own`.

## 6. Tests and gate

`gcc/testsuite/crug.dg/borrow/` holds 34 fixtures run by
`make check-BORROW` (DejaGnu, `borrow.exp`): 7 programs that must
run, 4 that must compile and dump or size-check, and 23 that must be rejected
with a specific code.

| check | script |
|---|---|
| the dg suite | `scripts/check-borrow-suite.sh` |
| zero cost | `scripts/check-borrow-cost.sh` |
| ABI identity | `scripts/check-borrow-abi.sh` |
| stable codes | `scripts/check-borrow-policy.sh` |
| dump shape | `scripts/check-borrow-dump.sh` |
| registry consistency | `scripts/check-diagnostics.sh` |

The exit gate for Phase 6 is in docs/crug/EXIT-GATES.md (amendment A-7).
