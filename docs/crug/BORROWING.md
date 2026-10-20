# Borrowing

Status: **Phases 6 and 7 implemented** (2026-10-21). The borrow model as a
user meets it: what a borrow is, the rules, lifetimes, calls, diagnostics and
limits. Reference: [BORROW.md](BORROW.md); decisions:
[adr/0019-interprocedural-borrow-policy.md](adr/0019-interprocedural-borrow-policy.md)
and [adr/0020-last-use-liveness.md](adr/0020-last-use-liveness.md).

Ownership answers "who destroys this?". Borrowing answers "may I look at or
write through this object, and who else is doing so at the same time?", checked
statically, without changing the object representation or adding a runtime check.

## 1. What a borrow is

A borrow is a scoped, checked reference to a whole owned object. There are two
kinds:

- `ref T *p` is a **shared** borrow: `p` may be read through, and other
  shared borrows of the same object may exist at the same time.
- `mut T *q` is a **mutable** borrow: `q` may read and write, and no other
  borrow of the object may exist while `q` is live.

Both are declaration specifiers, written before the type specifiers, exactly
like `own` ([GRAMMAR.md](GRAMMAR.md) section 2). A borrow is created by
initializing one from an intrinsic, or by receiving one as a parameter:

```c
void fill (mut char *buf, int n)
{
  for (int i = 0; i < n; i++)
    buf[i] = 'x';              /* writing through the mutable borrow */
}

void show (ref char *s)
{
  char first = *s;             /* reading through the shared borrow */
  (void) first;
}

void use (void)
{
  own char *line = malloc (16);
  ref char *alias = borrow (line);         /* shared borrow of line */
  mut char *writer = borrow_mut (line);    /* refused: alias is live */
  show (alias);
}
```

`borrow (e)` and `borrow_mut (e)` are intrinsics, not keywords: recognized only
as the callee of a call with no visible declaration, so a user function named
`borrow` keeps its C meaning ([adr/0018-grammar.md](adr/0018-grammar.md) D3). A
`ref T *` or `mut T *` parameter is a borrow from the caller, live for the body.

A borrow names an object, not a place inside one: the operand of
`borrow`/`borrow_mut` must be a whole owned variable (or an existing shared
borrow), and a declaration must be initialized from an intrinsic or another
borrow; members, elements and dereferences are refused ([BORROW.md](BORROW.md) §2).

Representation is plain C: `ref T *` and `mut T *` have the same size,
alignment and calling convention as `T *`, and `borrow (e)` lowers to `e`; the
qualifier disappears from the type. No runtime call, reference count or hidden
word is added ([ABI.md](ABI.md), [adr/0006-c-abi-policy.md](adr/0006-c-abi-policy.md)).

## 2. The rules

The central rule is **one mutable borrow, or any number of shared borrows, but
never both at once**. It is the same exclusivity rule a reader of C already
applies to `restrict`, made checkable.

The rest follows from it:

1. **A borrow needs an owner.** `borrow (p)` requires `p` to be an owned
   object with a live value. Borrowing an empty owner (`own T *p = 0`) is
   `crug-BORROW-011`.
2. **A mutable borrow needs a mutable source.** `borrow_mut` requires an owned,
   modifiable lvalue that is not itself a borrow; a mutable borrow cannot be
   initialized from a shared borrow (`crug-BORROW-012`), and a mutable borrow
   of a borrow is not supported (`crug-BORROW-010`).
3. **Borrows are whole-object.** A member (`p->field`, `s.field`), an array
   element, a dereference or an ordinary pointer is not a borrowable object in
   this phase (`crug-BORROW-009`).
4. **A borrow is an automatic variable.** It cannot be stored in memory, a
   struct or a static, or returned (`crug-BORROW-018`, `crug-BORROW-019`).
5. **A borrow is initialized where it is declared.** A `ref`/`mut`
   declaration without an initializer is `crug-BORROW-017`; a use before
   initialization is `crug-BORROW-013`.
6. **A shared borrow is read-only.** Assigning through a `ref` is
   `crug-BORROW-008`; writing through a `mut` is the point of the mutable
   borrow.

While a borrow is live, the owner is constrained. The matrix is the one to
memorize:

| Operation on the owner | While a shared borrow is live | While a mutable borrow is live |
| --- | --- | --- |
| read the owner | allowed | `crug-BORROW-015` |
| write the owner | allowed | allowed through the `mut` borrow only |
| create another shared borrow | allowed | `crug-BORROW-016` |
| create a mutable borrow | `crug-BORROW-002` | `crug-BORROW-003` |
| `move` or `release` the owner | `crug-BORROW-004` | `crug-BORROW-004` |
| `drop` the owner | `crug-BORROW-005` | `crug-BORROW-005` |
| assign a new value to the owner | `crug-BORROW-014` | `crug-BORROW-014` |
| let the scope end and destroy the owner | allowed (the borrow dies first) | allowed |

The last row is deliberate: destruction at scope end is not a conflicting
write, because the borrows declared there end before the objects are released.

`-fcrug-alias-facts` does not change these rules; it adds a code-generation
fact — a `mut` parameter becomes `restrict` — and proves the exclusivity the
optimizer is told about. Three diagnostics exist only with that option: a plain
pointer at a `mut` parameter (`crug-BORROW-021`), a shared borrow there
(`crug-BORROW-022`), and a lender beside its own mutable borrow
(`crug-BORROW-023`). Without it, a plain pointer at a `mut` parameter is
allowed as the migration path ([OPTIMIZATION.md](OPTIMIZATION.md)).

## 3. Lifetimes and last use

A borrow's lifetime starts at its declaration and ends after the **last
statement that reads it**, not at the closing brace
([adr/0020-last-use-liveness.md](adr/0020-last-use-liveness.md)). The checker
computes this backwards per function: live-in is uses plus live-out minus
definitions, and live-out is unioned at joins. A borrow not read after a point
is dead there, so the owner may be moved, mutated or dropped after it.

```c
void f (void)
{
  own char *p = malloc (8);
  ref char *alias = borrow (p);
  char c = *alias;           /* last read of alias */
  (void) c;
  drop (p);                  /* accepted: alias is dead here */
}
```

Three consequences show up in real code:

- **Joins union.** A borrow live on either side of a branch is live after the
  join; one path is enough.
- **Loops are conservative.** There is no fixpoint in this phase: every borrow
  declared in a function is live inside a loop body, and a borrow live after a
  loop is not widened; a loop-carried liveness fact is over-approximated.
- **`goto` abandons liveness.** A `goto` falls back to the lexical rule for the
  whole function: a borrow is live until the end of its block. This may report a
  conflict a last-use analysis would allow; it never misses one.

A borrow cannot escape its block: storing it in an aggregate, a static or
memory reached through a pointer is `crug-BORROW-018`, returning it
`crug-BORROW-019`. `-fcrug-dump=borrow` writes the computed ranges to
`<stem>.crug-borrow`, one `borrow` line per start and one `end` per end:

```
crug borrow dump v1
borrow t.c:7: alias shares p
end t.c:9: alias shares p (last-use)
```

## 4. Borrows across calls

The interprocedural policy is **depth zero**
([adr/0019-interprocedural-borrow-policy.md](adr/0019-interprocedural-borrow-policy.md)):
a function knows only the declared parameter and return types; there are no
summaries, and a call never extends, ends or creates a borrow.

- A borrow may be passed **only** to a `ref` or `mut` parameter
  (`crug-BORROW-020`); even a callee that only reads cannot be proven to, so an
  unannotated parameter is an error.
- A shared borrow may be passed where a `ref` parameter is expected. A `mut`
  parameter takes `borrow_mut (x)` or, by default, a plain pointer to an owned
  object; `-fcrug-alias-facts` rejects the plain pointer (`crug-BORROW-021`)
  and a shared borrow (`crug-BORROW-022`).
- Two borrows of one lender in a single call are allowed only when both are
  shared and go to `ref` parameters; if either is mutable the call is
  `crug-BORROW-002`/`crug-BORROW-003`. Passing the lender itself beside its
  own mutable borrow is `crug-BORROW-023` under `-fcrug-alias-facts`.
- A borrow is never returned (`crug-BORROW-019`) and never stored in memory
  (`crug-BORROW-018`). The workaround is to keep the owner and pass plain
  pointers inside one function, or to state the contract at a C boundary.
- A `ref T *` or `mut T *` parameter is live for the whole callee body, with an
  unknown lender; the callee may reborrow a `ref` parameter
  (`ref T *q = borrow (p);`) but may not move, drop or free it. A `mut`
  parameter is not reborrowable (ADR 0018 A-5).

A `.crugffi` contract is the one place a call into foreign code carries borrow
facts: `borrows argN` accepts a shared borrow or an un-borrowed owned value, and
`mut_borrows argN` a mutable borrow ([FFI.md](FFI.md) section 1). The rules are
the ones above; the contract is how the compiler learns what the C function does.

## 5. Diagnostics

Borrow diagnostics are errors in both the default and the strict mode, as the
registry records; the two with a `-Wcrug-*` spelling use it for
`-Werror`-style control rather than to make them warnings
([DIAGNOSTICS.md](DIAGNOSTICS.md)).

| Code | Trigger |
| --- | --- |
| `crug-BORROW-001` | a function returns a borrow (option: `-Wcrug-returned-borrow`) |
| `crug-BORROW-002` | a mutable borrow is created while a shared borrow is live |
| `crug-BORROW-003` | a second mutable borrow of the same object |
| `crug-BORROW-004` | the owner is moved or released while borrowed |
| `crug-BORROW-005` | the owner is dropped while borrowed |
| `crug-BORROW-006` | `borrow ()` operand is not an lvalue (option: `-Wcrug-intrinsic-operand`) |
| `crug-BORROW-007` | a borrow escapes the lifetime of its referent |
| `crug-BORROW-008` | assignment through a shared borrow |
| `crug-BORROW-009` | the operand is not a whole owned object or a borrow (member, element, dereference, plain pointer) |
| `crug-BORROW-010` | a mutable borrow of a borrow, or of a non-owned object |
| `crug-BORROW-011` | borrowing an owner that owns nothing |
| `crug-BORROW-012` | a mutable borrow initialized from a shared borrow |
| `crug-BORROW-013` | a borrow used without being initialized on this path |
| `crug-BORROW-014` | assigning to the owner while it is borrowed |
| `crug-BORROW-015` | using the owner while it is mutably borrowed |
| `crug-BORROW-016` | creating a borrow while the owner is mutably borrowed |
| `crug-BORROW-017` | a borrow declaration not initialized from `borrow`, `borrow_mut` or another borrow |
| `crug-BORROW-018` | a borrow that is not an automatic variable, or is stored in memory |
| `crug-BORROW-019` | a borrow returned from the function that created it |
| `crug-BORROW-020` | a borrow passed to a parameter that is not `ref` or `mut` |
| `crug-BORROW-021` | with `-fcrug-alias-facts`: a plain pointer at a `mut` parameter |
| `crug-BORROW-022` | with `-fcrug-alias-facts`: a shared borrow at a `mut` parameter |
| `crug-BORROW-023` | with `-fcrug-alias-facts`: the lender passed beside its own mutable borrow |

Every message names the borrow and lender where it can; notes report where the
borrow was created. Codes are stable; fixtures live in `crug.dg/borrow/` and
`crug.dg/diagnostics/`.

## 6. Limits

The Phase 7 implementation is deliberately smaller than the borrow model in
the specification. The limits below are known and are the ones a user is
likely to hit:

- **No place-level borrows.** Members, array elements and dereferences cannot
  be borrowed; `borrow (p->field)` is `crug-BORROW-009`, not a borrow of
  `field`.
- **No reborrowing of a mutable parameter.** A `mut` parameter cannot be
  borrowed mutably again inside the callee; a `ref` parameter can be borrowed
  sharedly.
- **No call-site ownership check.** A function that receives `ref T *` cannot
  prove that the caller's object is still owned; it sees a live borrow with an
  unknown lender. The FFI layer is the only source of cross-boundary facts.
- **No exterior/interior mutability story.** `_Atomic` and `volatile`
  objects are neither borrowed nor excluded from borrowing; a borrow of an
  atomic object is not tracked specially.
- **Conservative loops and the `goto` fallback** can reject a program whose
  liveness is in fact simple. The dump exists so the over-approximation can be
  seen.
- **Kernel mode rejects borrowing.** `-fcrug-kernel` cannot be combined with
  `-fcrug-borrow` in this release; kernel sources compile as plain GNU C
  ([adr/0017-kernel-ownership-policy.md](adr/0017-kernel-ownership-policy.md)).
- **No aliasing facts by default.** `-fcrug-alias-facts` is opt-in because it
  changes code generation; without it, the checker still enforces the borrow
  rules but does not give the optimizer a `restrict` fact
  ([adr/0010-noalias-lowering.md](adr/0010-noalias-lowering.md)).

When a program needs something the model does not express, the intended escape
is a plain pointer (or `raw` under strict mode) at a function boundary, not a
cast that hides it; that keeps the unchecked part visible to the next reader.
