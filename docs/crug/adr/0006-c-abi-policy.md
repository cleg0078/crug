# ADR 0006 - C ABI Policy

- **Status:** Accepted (2026-10-19)
- **Ratified:** 2026-10-19 by CLEG0078 (project owner)
- **Date:** 2026-10-19
- **Deciders:** project owner (ratification), frontend, FFI, kernel-profile and optimizer owners
- **Context docs:** RFC-0001 sections 14, 16, 17, 22, 23, 24 and 25; MASTER_SPEC
  sections 9, 17.5, 19.3, 22, 34.3 and 47; RUST_MEMORY_GAPS section 17;
  PRODUCTION_ROADMAP phases 4, 9, 10, 11 and 23; ADR 0003 D3; ADR 0004; ADR 0005;
  ADR 0018 D2 and D7

## Context

Three obligations meet at every ABI boundary:

1. C compatibility. A Crug translation unit must link against, and be linkable
   from, ordinary C: the same layouts, the same calling and return
   representations, the same symbol names, and no recompilation of existing
   `.a`/`.so` files (RFC-0001 section 22; MASTER_SPEC section 9).
2. Ownership soundness. Qualifiers are erased for type compatibility, but
   ownership is real: a boundary that silently loses an owner creates a double
   free, and a boundary that silently gains one creates a leak (ADR 0003).
3. Zero hidden runtime. No hidden field, flag, reference count, parameter or
   wrapper may be inserted to make the first two easier (RFC-0001 section 22;
   ADR 0003 D5).

Crug also introduces types that C does not have: `slice<T>`,
`option<T>`, `result<T,E>`, payload enums and `chan T`.
RFC-0001 section 22 allows them a Crug-specific ABI but requires it to be
documented before stability is claimed. Today no such document exists, while the
gate set already depends on one: Phase 10's check compiles
`gcc/testsuite/crug.dg/safe-types/layout.c`, which
`_Static_assert`s every size and alignment recorded in
`docs/crug/ABI.md`, and Phase 9's gate requires metadata never to change
binary ABI.

This ADR sets the policy and creates the registry. It does not freeze the layout
of any Crug-native type: those rows are filled in by the phases that introduce
the types, with the same commit that introduces them.

## Decision

### D1 - There are exactly two ABI classes

Every type is in exactly one class:

- **C-ABI-transparent**: the C ABI is a hard guarantee, testable against baseline
  GCC on every target. This covers all C types and every type whose only Crug
  difference is a qualifier.
- **Crug-native**: no C ABI is claimed. The type has a documented Crug ABI,
  recorded in `docs/crug/ABI.md`, and MAY cross into C only through an
  explicit lowering named there or in `.crugffi` metadata (ADR 0007).

There is no third class and no "probably compatible" mode. A type whose C claim
is not recorded is Crug-native by default.

### D2 - What C-ABI-transparent guarantees

For a C-ABI-transparent type, adding Crug qualifiers MUST NOT change any of the
following, compared with the same declaration compiled by the baseline GCC
recorded in ADR 0001:

- `sizeof`, `_Alignof`, `offsetof` of every member, and
  the presence and position of padding;
- the calling representation of parameters and return values, including
  aggregates passed or returned by value, and small-aggregate classification;
- variadic call behaviour, including default argument promotions;
- function-pointer representation and compatibility: `void (*)(own Foo *)`
  is compatible with `void (*)(Foo *)`;
- symbol names and linkage: Crug adds no mangling and no name decoration, and an
  external identifier keeps the symbol the C compiler would emit;
- static and thread-local storage size and layout;
- bit-field width, placement and the target's allocation rules; packed and
  explicitly aligned layouts; `_Atomic` semantics and layout;
- the C ABI of the target as selected by the GCC configuration, not a Crug
  variant of it.

Debug information may describe ownership. That is not object state and does not
change layout (ADR 0003 D1).

### D3 - Erasure is a type rule, not an escape hatch

A qualifier is erased for assignment compatibility, parameter lists,
function-pointer assignment, composite types, `_Generic` and linkage.
Erasure does not erase checking: inside Crug code the ownership rules still
apply to the erased type (ADR 0004).

A value crossing a boundary where the other side has no Crug declarations is
treated as `raw`. It MUST produce `-Wcrug-ffi-ownership`
(`crug-OWN-008`), and MUST be an error under `-fcrug-strict`.
Silently dropping the owner at such a boundary is the defect this rule exists to
prevent. The function-pointer form of the same loss (calling through a pointer
whose declared parameters carry no ownership) is diagnosed the same way; the C
type compatibility itself is never rejected.

### D4 - Crossing a boundary moves bits, never code

Moving an owned value across a call boundary is a bitwise copy plus a logical
transfer of ownership. It MUST NOT:

- allocate, lock, or call a helper (RFC-0001 section 16);
- change the argument list, add a hidden parameter, change the return
  mechanism, or wrap the function;
- emit a hidden destructor call in the callee. The destructor runs where the
  owner lives, under the rules of ADR 0005;
- force a spill or a copy the C ABI would not require.

An owned aggregate passed or returned by value uses exactly the C classification
of its underlying struct (ADR 0003 D3). A move that the C ABI would already
perform as a copy costs nothing extra.

### D5 - Crug-native types have no C ABI until the registry says otherwise

For `slice<T>`, `option<T>`, `result<T,E>`, payload enums
and `chan T`:

- the type is Crug-native from the moment it exists;
- its size, alignment, member layout, calling representation and the
  representation of an inactive variant MUST be recorded in
  `docs/crug/ABI.md` by the phase that ships it, before that phase's gate
  can pass;
- until recorded, it MUST NOT cross an external interface. Crossing requires an
  explicit lowering: for a slice, the metadata may name two C parameters
  `(T *ptr, size_t len)`, because a slice is not automatically
  ABI-equivalent to two parameters (MASTER_SPEC section 19.3, RFC-0001
  section 25.1); for the others, the lowering states the C shape it maps to;
- a by-value use inside Crug code follows the documented Crug layout, not the
  C classification of a guessed C twin;
- niche optimization is not used in the MVP (RFC-0001 section 25.2), so no
  `option<T>` layout may depend on an invalid-value niche until the
  registry records one.

Freezing a layout is a one-way door. A layout that ships without a registry row
becomes de-facto ABI that cannot be fixed without a major release; that is the
risk Phase 10's gate names.

### D6 - The registry, and who may change it

`docs/crug/ABI.md` is the single normative registry of ABI facts. This ADR
states the rules; the registry states the facts per type and per target.

- Every row carries the type, the target, the phase that froze it, and the test
  that asserts it.
- The commit that introduces a Crug-native type also adds its registry rows and
  its `layout.c` assertions. A type without rows does not pass the phase.
- Changing a recorded layout is an ABI change: it needs an ADR, a major ABI
  version bump, and a migration note. Adding a new target is not a change to an
  existing target's rows.
- A claim that exists only in a test, a comment or code is not a claim. If
  `ABI.md` does not say it, C does not get it.

### D7 - Targets, baselines and neighbouring ABIs

- Primary target: `x86_64-pc-linux-gnu` with the System V ABI, matching
  the GCC 16.2.0 baseline (ADR 0001). AArch64 is a Phase 8 deliverable and gets
  its own registry rows; a rule that holds on x86-64 is not assumed elsewhere.
- Kernel mode (`-fcrug-kernel`) preserves the kernel ABI: object layout,
  bit-field behaviour, packed and aligned layout, section attributes, symbol
  names, calling conventions and inline-asm constraints (MASTER_SPEC
  section 17.5). No Crug metadata may appear in a kernel-visible struct.
- `libcrug`'s ABI (PAL v1, Phase 13) is a separate contract and is out of
  scope here. Its internal symbols use the reserved prefix `__crug_`
  (MASTER_SPEC section 34.3); ordinary Crug objects must contain none of them
  (Phase 2 gate).
- C library and third-party ABI is never modified: existing `.a` and
  `.so` files need no recompilation to be called (RFC-0001 section 23).

### D8 - Optimizer facts must be proven, not assumed from erasure

ABI transparency does not grant aliasing facts. `mut` may feed the
optimizer only where exclusivity is proven (Phase 11), `ref` grants
nothing that `const` would not, and `own` does not imply
`noalias` by itself. `restrict` keeps exactly its C meaning, and a
false `noalias` remains a P0 defect (RFC-0001 section 40). The compiler
MUST NOT assume provenance or lifetime facts the language does not guarantee
(RUST_MEMORY_GAPS section 17.4).

## Consequences

- Phase 4 can test ABI transparency mechanically: layouts, calling
  representation, symbol names and function pointers against baseline GCC.
- Phase 9 has a rule for what FFI metadata may and may not do: it describes
  ownership, never layout (`diff-obj` stays at zero differences).
- Phase 10 gets a definition of "documented": a row in `ABI.md` plus its
  `layout.c` assertion.
- Phase 11 gets a boundary on what qualifiers may tell the optimizer.
- The registry is a live document, not a release-time artifact; it exists from
  now and starts with the C-ABI-transparent facts that are already true.
- Test obligations: `crug.dg/abi/` (layout twins, call/return both
  directions, function pointers, varargs, symbol names), `crug.dg/abi/ffi-*`
  (boundary diagnostics), and the Phase 10 layout assertions.

## Alternatives rejected

- **One ABI class, Crug-native types described as "C structs with a tag".** It
  would freeze a layout nobody decided, and C callers would silently depend on
  it.
- **Pass Crug-native types as opaque pointers.** Safe but unusable for slices
  and results at the FFI boundary, and it does not answer what by-value calls do
  inside Crug code.
- **Make a slice ABI-equivalent to two parameters.** Rejected by MASTER_SPEC
  section 19.3 and by every calling convention that splits aggregates
  differently from separate parameters.
- **Mangle ownership into the symbol name.** Changes linkage and the C ABI, and
  C has no mangling to hook into (ADR 0003, alternatives).
- **A hidden descriptor or return pointer for Crug-native returns.** Violates
  D4 and the zero-runtime obligation.
- **Stabilize layouts at first implementation and document later.** That is the
  failure mode Phase 10's gate risk statement describes; the registry makes the
  documentation a precondition, not a follow-up.
- **Per-call-site ABI opt-in (`__attribute__((crug_abi))`).** A second
  ABI dialect with no owner and no test story; the registry plus metadata covers
  the same need.

## Open questions

1. Exact `slice<T>` layout: the shape is pointer plus length, but the
   field order, length type and whether it is one struct or two registers for
   calling are frozen in `ABI.md` at Phase 10.
2. `option<T>` layout and the first permitted niche, if any, when Phase
   10 records it.
3. `result<T,E>` layout and return convention: RFC-0001 section 25.3
   requires a focused RFC before it ships.
4. `chan T` representation (handle width, opacity) at Phase 13/14.
5. AArch64 classification rows at Phase 8, including the aggregate rules that
   differ from SysV.
6. Whether an ABI version macro or `-fcrug-abi=N` flag is needed before
   1.0; Phase 25's packaging check prints separate compiler, language and runtime
   ABI versions.
