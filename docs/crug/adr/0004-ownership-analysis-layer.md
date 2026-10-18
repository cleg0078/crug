# ADR 0004 - Ownership Analysis Layer: Aggregates, Places, Drops and Adoption

- **Status:** Accepted (2026-10-18)
- **Date:** 2026-10-18
- **Ratified:** 2026-10-18 by CLEG0078 (project owner)
- **Deciders:** project owner (ratification), frontend + ownership pass owners
- **Context docs:** RFC-0001 sections 14, 16, 17, 22 and 25.4; MASTER_SPEC section 13;
  RUST_MEMORY_GAPS sections 4 to 7, section 18 and section 41; ADR 0003; ADR 0005

## Context

ADR 0003 fixes how ownership is represented: static metadata, no hidden object
state, qualifiers erased at ABI boundaries. This ADR fixes the analysis rules
built on that representation: what counts as an owning type, which operations
move or copy it, how aggregates and their fields are tracked, what is dropped and
in which order, and how ownership is created and released.

The rules exist because a struct with `own` members creates a double-free hole:
a plain C assignment or `memcpy` duplicates the pointer without transferring
ownership, and both copies drop it. A hidden moved bit would close the hole but is
forbidden (ADR 0003 D3); a runtime drop flag would violate zero-cost (ADR 0003
D4). The hole is therefore closed by rejecting the ambiguous operations and the
unresolvable joins, which is a closed rule set rather than a default.

This record is the analysis half of the former aggregate-ownership ADR. The former
D2 (static move state and diagnostics) and D3 (ABI boundaries) moved to ADR 0003
as D2 and D3. They keep their numbers there and are not repeated here. The
decision numbers D4 to D10 below are unchanged from the original record, so
citations in ADR 0018, `docs/crug/DIAGNOSTICS.md` and `docs/crug/EXIT-GATES.md`
keep their meaning.

## Decision

### D1 - Aggregates inherit owned-ness

A struct, union or array type is *owning* if it has, at any depth, a member with
an `own` qualifier. Any value whose type is owning is non-copyable: assignment,
parameter passing by value from an lvalue, and returning from an lvalue are
ill-formed unless the source is moved explicitly (`move(x)`) or is an rvalue that
the compiler knows is a move (a call result, an aggregate literal returned by
`return`, or a `move` expression).

The static-diagnostic rule for these operations is ADR 0003 D2.

### D4 - Field places and partial moves (MVP scope)

MVP tracks places at variable granularity plus one dereference level (`p->a`,
`s.a`) when the aggregate is statically known to be a whole value. It does
**not** support moving a single field out of an aggregate and leaving the parent
partially initialized (conflict C-01). `move(s.field)` is rejected in MVP with
`note: partial moves are post-MVP; move the whole aggregate`.

Rationale: partial moves require per-field state plus a "partially moved"
aggregate state and a drop test at scope exit, which is exactly the hidden state
forbidden by ADR 0003 D3. The GAPS partial-initialization model (section 6) is a
post-MVP target tracked as M-MEM-2; it becomes legal only with an ADR that also
answers the drop-test question.

### D5 - Drop order for aggregates

At the end of the lifetime of an owning aggregate, its owned members are dropped
in **reverse declaration order**, recursively, before any drop of the enclosing
object's own container. Arrays drop from the **highest index to the lowest**.
Unions are covered by D6. This turns the "Recommended direction" of
RUST_MEMORY_GAPS section 22 into a rule: the drop of a member is the drop of the
value it contains, and a NULL owned pointer is skipped (D8).

### D6 - Unions containing owned members are rejected

`union U { own char *p; int n; };` is an error in MVP
(`error: own member in union requires an active-variant contract`). A union
cannot tell the compiler which member is live, so neither drop nor move can be
checked. The escape is to make the union opaque to Crug (`raw` members) or to
use a tagged struct. This settles the four-option menu in RUST_MEMORY_GAPS
section 24 with the conservative answer.

### D7 - Arrays

`own T a[N]` declares N owned elements. The array is move-only; moving it moves
all elements. `sizeof` is `N * sizeof(T)`. Element-wise move (`move(a[i])`) is a
partial move and therefore D4. A pointer-to-owned-array decays to `own T *` only
via an explicit `move`.

### D8 - Destructors are adopted, not inferred

An owned pointer has a destructor exactly when one is attached:

- `own malloc(...)`-shaped values from the standard allocation catalogue
  (malloc, calloc, strdup, and friends) get `free`;
- everything else requires `adopt(expr, dtor)` with `dtor` of type
  `void (*)(T *)`;
- an owned pointer with no adopted destructor leaks by design and is reported by
  `-Wcrug-unadopted-own` in strict mode.

A NULL owned value is skipped at drop time, because custom destructors are not
required to accept NULL (a documented deviation from `free(NULL)` being
harmless).

### D9 - memcpy and byte views

`memcpy`, `memmove`, `memset` and casts to `char *` over an owning object are
**undefined behaviour** at the language level: they can duplicate or destroy
ownership. The compiler diagnoses the identifiable forms (constant size, known
source type) with `-Wcrug-owned-byte-copy`; under `-fcrug-strict` they are errors
unless the object is `raw`. This must be stated in the documentation rather than
pretended away: it is the one remaining hole, and it is reachable only through an
explicit byte-level operation.

### D10 - Adoption, release and re-adoption

`adopt(expr, dtor)` is the only way to attach a destructor to a pointer that the
allocation catalogue does not cover (D8). `release(expr)` is the only operation
that gives ownership up without dropping, and it is the documented workaround for
the missing copy mechanism (conflict C-10):
`adopt(release(x), dtor)` is a deliberate "copy" written by the programmer, with
two visible destructor decisions.

## Consequences

- The ownership pass is a static dataflow problem over the existing CFG, with no
  new IR and no runtime support (satisfies the K0 and O1 zero-runtime
  requirements); ADR 0003 fixes the storage side.
- Several legal-looking C programs become errors. That is intended: the error is
  the safety mechanism.
- The frontend must publish an aggregate-ownership query API for the borrow pass
  (a type is owning; a field is mixed or owned).
- Tests: `crug.dg/ownership/aggregate-*` (move of struct, rejected copy, rejected
  partial move, rejected union, drop order, adopt/release, memcpy diagnostic),
  plus an ABI test comparing `offsetof` and `sizeof` against plain C.

## Alternatives rejected

- **Hidden moved bit or drop flag.** Breaks RFC-0001 section 22 and O1 zero-cost;
  makes the struct ABI differ from C (ADR 0003 D3).
- **Copy-on-write or reference counting for aggregates.** Explicitly forbidden
  (RFC-0001 section 22; RUST_MEMORY_GAPS section 18).
- **Allow partial moves in MVP.** Requires either runtime drop tests or per-path
  forking of the type state; deferred, not hidden.
- **Treat every struct as owning if any field is a pointer.** Rejects valid C
  usage and makes `raw` useless.
