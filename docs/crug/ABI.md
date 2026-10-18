# CRUG ABI registry

**Status:** Draft - governed by ADR 0006 (Proposed). No Crug-native layout is
frozen yet; the rows below mark what is guaranteed today and what a phase must
fill in before its gate can pass.

This file is the single normative registry of ABI facts (ADR 0006 D6). A claim
that is not in this file is not an ABI claim. Rules live in ADR 0006; facts live
here.

## 1. How a row gets frozen

A row moves from `Pending` to `Frozen` only when all of the
following are in one commit:

1. the type or construct exists in the compiler;
2. this file records size, alignment, member layout, calling representation and
   inactive-variant representation, as applicable;
3. `gcc/testsuite/crug.dg/safe-types/layout.c` asserts every recorded
   size and alignment with `_Static_assert`s (Phase 10 gate);
4. the phase whose gate requires the type names the row.

Changing a frozen row is an ABI change: ADR, major version bump, migration note.

## 2. C-ABI-transparent constructs (guaranteed now)

These are guarantees, not pending items. They are tested against the baseline
GCC recorded in ADR 0001 and must hold on every supported target. The fixture
paths below are the standing obligations; where the construct is plain C today,
the existing C testsuite and `scripts/diff-cc.sh` already assert the same facts.

| Construct | Guarantee | Where it is asserted |
|-----------|-----------|----------|
| `own`, `ref`, `mut`, `raw` pointer qualifiers | Same size, alignment and calling representation as the unqualified pointer; erased for type compatibility, prototypes, function-pointer assignment, `_Generic` and linkage | `crug.dg/ownership/abi-ptr.c` (Phase 4 gate), `crug.dg/abi/qualifier-erasure.c` |
| Qualified `struct` members | Byte-identical layout to the C struct, including padding; no hidden field, bit-field or state byte | `crug.dg/abi/layout-twins.c` |
| Qualified function parameters and returns | Same machine argument and return representation as the unqualified declaration; no hidden parameter, no wrapper | `crug.dg/abi/call-both-directions.c` |
| Owned aggregate by value | C classification of the underlying struct; move is a bitwise copy plus logical transfer, no helper call | `crug.dg/abi/agg-by-value.c` |
| Variadic calls | C default argument promotions and register/stack assignment unchanged | `crug.dg/abi/varargs.c` |
| Symbol names and linkage | No mangling or decoration; an `extern` identifier keeps the C symbol | `crug.dg/abi/symbols.c` |
| Static and thread-local storage | Same size, alignment and TLS model as C | `crug.dg/abi/tls.c` |
| Bit-fields, packed, aligned, `_Atomic` | Target rules unchanged; `_Atomic` keeps C semantics (RFC-0001 section 21) | `crug.dg/abi/c-layout.c` |
| Kernel-visible structs | No Crug metadata in any kernel-visible type; kernel ABI preserved (MASTER_SPEC section 17.5) | `gcc/testsuite/crug.dg/kernel/` plus Phase 19 corpus |

## 3. Crug-native type registry

`none` of these has a C ABI. Each has a Crug ABI that becomes stable only
when its row is frozen; until then it cannot cross an external interface without
an explicit lowering (ADR 0006 D5).

| Type | Phase | Size / alignment | Layout | Calling representation | Inactive variant | Status |
|------|-------|------------------|--------|------------------------|------------------|--------|
| `slice<T>` | 10 | pending | pointer plus length; field order, length type pending | pending (not two parameters; MASTER_SPEC section 19.3) | n/a | Pending |
| `option<T>` | 10 | pending (may exceed `T`) | pending | pending | pending | Pending |
| `result<T,E>` | focused RFC (RFC-0001 section 25.3) | pending | pending | pending | pending | Pending |
| payload `enum` | 10 | pending | tag plus payload union; tag width and alignment pending | pending | representation must be documented (RFC-0001 section 22) | Pending |
| `chan T` | 13-14 | pending | handle representation pending | pending | n/a | Pending |

A pending row is a promise to decide, not a decision. In particular, no code may
depend on a guessed layout for any of these types.

## 4. Targets

| Target | ABI | Baseline | Status |
|--------|-----|----------|--------|
| `x86_64-pc-linux-gnu` | System V AMD64 | GCC 16.2.0 (ADR 0001) | Primary; C-ABI-transparent rows apply |
| `aarch64-linux-gnu` | AAPCS64 | GCC 16.2.0 | Phase 8; own rows required, no assumption from x86-64 |
| kernel x86_64 / aarch64 | kernel ABI | docs/crug/KERNEL-BASELINE.md | Phase 19; defers to KERNEL-COMPATIBILITY |

## 5. FFI boundary

- An unannotated C declaration is `raw`: unknown ownership is unmanaged
  (RFC-0001 section 23).
- An owner crossing that boundary produces `-Wcrug-ffi-ownership`
  (`crug-OWN-008`); under `-fcrug-strict` it is an error
  (ADR 0006 D3).
- `.crugffi` metadata describes ownership and never layout: binary output
  with and without metadata is identical (Phase 9 gate).
- A Crug-native type crossing into C names its lowering; for a slice the
  permitted lowering is two parameters `(T *ptr, size_t len)`. The
  metadata grammar and version are ADR 0007's subject.

## 6. Versioning

- The C ABI version is the baseline GCC's ABI as selected by the target.
  Crug-native types get their own documented ABI version when the first row is
  frozen.
- Current claim: **none of the Crug-native types is stable**, so there is no
  Crug-native ABI version to break yet.
- `crugc --version` must eventually print compiler, language and Crug
  ABI versions separately (Phase 25 packaging check); until the first frozen row
  the ABI version is reported as `0` (pre-stable).

## 7. Test obligations

| Obligation | Location |
|------------|----------|
| Layout twins versus baseline GCC (`sizeof`, `_Alignof`, `offsetof`, padding) | `gcc/testsuite/crug.dg/abi/layout-twins.c` |
| C caller to Crug callee and back, including aggregates | `gcc/testsuite/crug.dg/abi/call-both-directions.c` |
| Function-pointer compatibility after erasure | `gcc/testsuite/crug.dg/abi/qualifier-erasure.c` |
| Symbol names against `nm` | `gcc/testsuite/crug.dg/abi/symbols.c` |
| Every frozen size/alignment in this file | `gcc/testsuite/crug.dg/safe-types/layout.c` |
| Metadata changes no binary ABI | `scripts/diff-obj.sh gcc/testsuite/crug.dg/ffi/ "-O2" "-O2 -fcrug-ffi"` |

## 8. Changelog

| Date | Change |
|------|--------|
| 2026-10-19 | File created with ADR 0006. C-ABI-transparent rows recorded; all Crug-native rows pending. |
