# ADR 0003 - Ownership Metadata Storage

- **Status:** Accepted (2026-10-18)
- **Date:** 2026-10-18
- **Ratified:** 2026-10-18 by CLEG0078 (project owner)
- **Deciders:** project owner (ratification), frontend + ownership pass owners
- **Context docs:** RFC-0001 sections 7.3, 14, 16, 17 and 22; MASTER_SPEC section 13;
  RUST_MEMORY_GAPS sections 6, 7 and 18; ADR 0004; ADR 0005

## Context

Crug requires three things at once:

1. struct layout, `sizeof` and the calling representation stay identical to C
   (RFC-0001 section 22);
2. owned values are move-only and are dropped deterministically (RFC-0001
   sections 17 and 7.3);
3. the compiler must not add hidden struct fields or hidden reference counts
   (RFC-0001 section 22).

If a struct contains `own` members, a plain C assignment or `memcpy` copies the
pointer without transferring ownership, and both copies drop it: a double free.
A hidden "moved" bit would fix this but is forbidden by (3); a runtime drop flag
would violate zero-cost (MASTER_SPEC section 19, O1 "no hidden runtime"). The
three requirements are jointly satisfiable only if ownership is carried by the
program's declarations and static analysis rather than by the value at run time,
and if the ambiguous operations are rejected (ADR 0004).

This ADR fixes the storage model: what exists at compile time, what exists in the
object, and what crosses an ABI boundary. ADR 0004 fixes the analysis rules
(places, moves, aggregates); ADR 0005 fixes where drops are lowered. This record
is the metadata half of the former aggregate-ownership ADR; the split is recorded
as C-11 in `spec/000-PRECEDENCE.md`, and the decision numbers D2 and D3 are
retained so that existing citations keep their meaning.

## Decision

### D1 - Ownership is static metadata, not object state

An `own`, `ref`, `mut` or `raw` qualifier is recorded on the declaration
and in the frontend type node. The ownership of a value at a program point is
derived from those declarations and from a dataflow analysis over the CFG.
Nothing is stored in the object, in the stack frame, in thread-local storage, or
in a side table keyed by address.

For every expression the frontend must be able to answer, at compile time:

- is the type owning (ADR 0004 D1)?
- which subobjects are owned (ADR 0004 D4 to D7)?
- is this place OWNED, MOVED, DROPPED, MAYBE_MOVED or PARTIALLY_MOVED at this
  program point (ADR 0004)?
- what destructor, if any, is attached (ADR 0004 D8)?

The last answer may name a run-time function pointer when it comes from
`adopt(expr, dtor)`, but which destructor applies to a given object is a static
fact.

### D2 - Copying an owning value is a compile-time diagnostic, never a runtime flag

`error: use of moved value` / `error: copy of owning aggregate 'struct S' is not
allowed; pass by pointer or use move()`. The check is static. No runtime state is
emitted, ever.

At control-flow joins, the move state of each owned variable is the meet of the
incoming states. If a drop is needed on a path where the state is MAYBE-MOVED, the
program is rejected: `error: 's' may be moved; unconditional drop at scope exit
cannot be proven`. The fix is either an explicit `drop()` on the path that moves
it, or a structural rewrite. This is the price of D1 and D3; the alternatives are
listed below.

### D3 - Qualifiers are erased at every ABI boundary

- Passing or returning an owning aggregate by value uses exactly the C ABI
  classification of the underlying struct type. The move is a bitwise copy plus a
  logical transfer; no helper call, no allocation, no hidden parameter (satisfies
  RFC-0001 section 16).
- An owning value crossing into a non-Crug translation unit is allowed only
  through `.crugffi` metadata that names the destructor or marks the field as
  opaque. Without metadata the boundary is treated as `raw` and produces
  `-Wcrug-ffi-ownership`; under `-fcrug-strict` it is an error. This is the
  only guard against C code double-freeing a Crug-owned pointer.
- `sizeof`, alignment and field offsets are byte-identical to the C definition.
  No padding is added, no field is reordered, and the qualifier does not change
  the type's identity for linkage or for type compatibility with C.
- A cast, a compound literal, a typedef or a function prototype cannot create or
  erase ownership. Ownership is created by an owning declaration, an owning
  return, `adopt()`, or `move()` (ADR 0018 D2 and D3); a typedef that carries a
  qualifier is rejected, a typedef used at the declaration site may be qualified.

### D4 - Drop decisions are compile-time facts, not materialised flags

Drop elaboration computes, per path, whether a drop is needed. Those decisions
live in the CFG and in the generated code as ordinary calls; they are never
stored in the object. Where a path cannot be proven to need or to skip a drop,
the program is rejected rather than instrumented (ADR 0005). An optimiser may
remove an inserted drop call, but it may not introduce runtime ownership state.

### D5 - Forbidden representations

Any of the following violates RFC-0001 section 22 and the O1 zero-runtime
obligation, and is forbidden even when it would make an analysis simpler:

- a hidden struct field, bit-field or padding byte that records move or drop
  state;
- a reference count, generation number or magic value stored in or next to the
  object;
- thread-local or global registries of owned objects, or tables keyed by
  address;
- a run-time drop flag, including one passed as a hidden parameter;
- ownership encoded in a mangled name, a wrapper struct, or a differently sized
  type;
- any helper call inserted for assignment, move, copy or drop bookkeeping.

## Consequences

- Ownership checking is a static dataflow problem over the existing CFG, with no
  new IR node and no runtime support (satisfies the K0 and O1 zero-runtime
  requirements).
- The frontend must publish an ownership query API for the borrow pass and the
  drop pass: a type is owning, a field is owned or mixed, a place's move state,
  and the attached destructor.
- Debug information may describe ownership; that is not object state and does
  not change layout.
- Tests: `crug.dg/ownership/abi-*` (offsets and `sizeof` against plain C,
  `objdump` shows no helper call for a move, no new TLS symbol), and
  `crug.dg/ownership/metadata-*` (owning-type and field queries exercised
  through diagnostics).

## Alternatives rejected

- **Hidden moved bit or drop flag.** Breaks RFC-0001 section 22 and O1
  zero-cost; makes the struct ABI differ from C.
- **Copy-on-write or reference counting for owned values.** Explicitly forbidden
  (RFC-0001 section 22; RUST_MEMORY_GAPS section 18).
- **A side table keyed by address, maintained by a runtime.** Hidden runtime,
  allocation on move, and it fails for values that are never address-taken.
- **Encode ownership in the type name and rely on C++-style mangling.** Changes
  linkage and the C ABI; C has no mangling to hook into.
