# ADR 0004 - Ownership of Aggregates, Containers and ABI Boundaries

- **Status:** Proposed
- **Date:** 2026-10-18
- **Deciders:** project owner (ratification), frontend + ownership pass owners
- **Context docs:** RFC-0001 s14 L465-487, s16 L510-532, s22 L641-668, s25 L747;
  MASTER_SPEC s13; RUST_MEMORY_GAPS s4-s7, s41

## Context

Crug requires three things at once:

1. struct layout, `sizeof` and the calling representation stay identical to C
   (RFC-0001 L487, L643, L666);
2. owned values are move-only and are dropped deterministically (L538, L244);
3. the compiler must not add hidden struct fields (L487).

If a struct contains `own` members, a plain C assignment or `memcpy` copies the
pointer without transferring ownership, and both copies drop it: a double free.
A hidden "moved" bit would fix this but is forbidden by (3); a runtime drop flag
would violate zero-cost (s19, O1 "no hidden runtime"). The three requirements are
jointly satisfiable only if the language forbids the ambiguous operations and
rejects the unresolvable joins. This ADR states exactly which operations are
legal, so that the ownership pass has a closed rule set instead of a default.

## Decision

### D1 - Aggregates inherit owned-ness

A struct, union or array type is *owning* if it has, at any depth, a member with an
`own` qualifier. Any value whose type is owning is non-copyable: assignment,
parameter passing by value from an lvalue, and returning from an lvalue are
ill-formed unless the source is moved explicitly (`move(x)`) or is an rvalue that
the compiler knows is a move (a call result, an aggregate literal returned by
`return`, or a `move` expression).

### D2 - Copying an owning aggregate is a diagnostic, not a runtime flag

`error: use of moved value` / `error: copy of owning aggregate 'struct S' is not
allowed; pass by pointer or use move()`. The check is static. No runtime state is
emitted, ever.

At control-flow joins, the move state of each owned variable is the meet of the
incoming states. If a drop is needed on a path where the state is MAYBE-MOVED, the
program is rejected: `error: 's' may be moved; unconditional drop at scope exit
cannot be proven`. The fix is either an explicit `drop()` on the path that moves
it, or a structural rewrite. This is the price of (3); see "Alternatives".

### D3 - ABI at boundaries

- Passing/returning an owning aggregate by value uses exactly the C ABI
  classification of the underlying struct type. The move is a bitwise copy plus a
  logical transfer; no helper call, no allocation, no hidden parameter
  (satisfies RFC-0001 L522, L523).
- An owning aggregate crossing into a non-Crug translation unit is only allowed
  through `.crugffi` metadata that names the destructor or marks the field as
  opaque. Without metadata the boundary is treated as `raw` and produces
  `-Wcrug-ffi-ownership`; under `-fcrug-strict` it is an error. This is the only
  guard against C code double-freeing a Crug-owned pointer.
- `sizeof`, alignment and field offsets are byte-identical to the C definition.
  No padding is added, no field is reordered.

### D4 - Field places and partial moves (MVP scope)

MVP tracks places at variable granularity plus one dereference level
(`p->a`, `s.a`) when the aggregate is statically known to be a whole value. It does
**not** support moving a single field out of an aggregate and leaving the parent
partially initialized (conflict C-01). `move(s.field)` is rejected in MVP with
`note: partial moves are post-MVP; move the whole aggregate`.

Rationale: partial moves require per-field state plus a "partially moved"
aggregate state and a runtime drop test at scope exit, which is exactly the hidden
state forbidden by (3). The GAPS document's partial-initialization model (s6) is a
post-MVP target tracked as M-MEM-2; it becomes legal only with an ADR that also
answers the drop-test question.

### D5 - Drop order for aggregates

At the end of the lifetime of an owning aggregate, its owned members are dropped in
**reverse declaration order**, recursively, before any drop of the enclosing
object's own container. Arrays drop from the **highest index to the lowest**.
Unions are covered by D7. This fixes the "Recommended direction" of
RUST_MEMORY_GAPS L1092 into a rule; the drop of a member is the drop of the value
it contains, and a NULL owned pointer is skipped (D8).

### D6 - Unions containing owned members are rejected

`union U { own char *p; int n; };` is an error in MVP
(`error: own member in union requires an active-variant contract`). A union cannot
tell the compiler which member is live, so neither drop nor move can be checked.
The escape is to make the union opaque to Crug (`raw` members) or to use a tagged
struct. This unblocks the four-option menu in GAPS s24 with the conservative
answer.

### D7 - Arrays

`own T a[N]` declares N owned elements. The array is move-only; moving it moves
all elements. `sizeof` is `N * sizeof(T)`. Element-wise move (`move(a[i])`) is a
partial move and therefore D4. A pointer-to-owned-array decays to `own T *` only
via an explicit `move`.

### D8 - Destructors are adopted, not inferred

An owned pointer has a destructor exactly when one is attached:

- `own malloc(...)`-shaped calls from the standard allocation catalogue (malloc,
  calloc, strdup, and friends) get `free`;
- everything else requires `adopt(expr, dtor)` with `dtor` of type
  `void (*)(T *)`;
- an owned pointer with no adopted destructor leaks by design and is reported by
  `-Wcrug-unadopted-own` in strict mode.

A NULL owned value is skipped at drop time, because custom destructors are not
required to accept NULL (documented deviation from `free(NULL)` being harmless).

### D9 - memcpy and byte views

`memcpy`, `memmove`, `memset` and casts to `char *` over an owning object are
**undefined behaviour** at the language level: they can duplicate or destroy
ownership. The compiler diagnoses the identifiable forms (constant size, known
source type) with `-Wcrug-owned-byte-copy`; under `-fcrug-strict` they are errors
unless the object is `raw`. This must be stated in the docs rather than pretended
away: it is the one remaining hole, and it is only reachable through an explicit
byte-level operation.

### D10 - Adoption, release and re-adoption

`adopt` is the only way to create ownership; `release(expr)` is the only way to
give it up and is the documented workaround for the missing copy mechanism
(conflict C-10): `adopt(release(x), dtor)` is a deliberate "copy" that the
programmer writes, with two visible destructor decisions.

## Consequences

- Ownership checking is a static dataflow problem over the existing CFG, with no
  new IR and no runtime support (satisfies K0 and O1 zero-runtime).
- Several legal-looking C programs become errors. That is intended: the error is
  the safety mechanism.
- The frontend must publish an aggregate-ownership query API for the borrow pass
  (a type is owning; a field is mixed or owned).
- Tests: `crug.dg/ownership/aggregate-*` (move of struct, rejected copy, rejected
  partial move, rejected union, drop order, adopt/release, memcpy diagnostic),
  plus an ABI test comparing `offsetof`/`sizeof` against plain C.

## Alternatives rejected

- **Hidden moved bit / drop flag.** Breaks RFC-0001 L487 and O1 zero-cost; makes
  the struct ABI differ from C.
- **Copy-on-write or refcount for aggregates.** Explicitly forbidden (L487, s18).
- **Allow partial moves in MVP.** Requires either runtime drop tests or
  per-path forking of the type state; deferred, not hidden.
- **Treat every struct as owning if any field is a pointer.** Rejects valid C
  usage and makes `raw` useless.
