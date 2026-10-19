# ADR 0021 - Strict Mode Policy: raw, unsafe and non-local control flow

- **Status:** Proposed
- **Date:** 2026-10-19
- **Phase:** 8 (strict mode, raw and unsafe)
- **Requires:** ADR 0003 (ownership metadata), ADR 0004 (aggregate ownership),
  ADR 0005 (drop lowering), ADR 0018 (surface grammar, D2/D3), ADR 0019
  (interprocedural borrow policy), RFC-0001 sections 7.1, 14, 18, 20 and 21.

## Context

RFC-0001 section 20 says that `raw` marks unchecked pointer semantics "in strict
contexts" and that `unsafe { ... }` marks code for which the programmer accepts
responsibility for additional invariants, with no inherent runtime cost. It never
defines which checks a strict context *adds*, and the token `-fcrug-strict` is
load-bearing in four places (borrow escapes, diagnostics promotion, the kernel
restriction and the conformance classes) without a decision record.

The Phase 8 exit gate lists the strict-mode prerequisite as "a policy ADR covering
raw dereference, integer-pointer conversion, owned-to-unmanaged conversion, inline
asm, setjmp/longjmp and unknown FFI". This ADR is that policy. It also fixes what
`unsafe` may suppress, which is the question a reader of section 20 cannot answer
from the RFC alone.

The implementation this ADR governs is in `gcc/c/crug-ownership.cc`,
`gcc/c/c-parser.cc` and `gcc/c/c-decl.cc`; the decisions below are what the code
must do, not a description of what it happens to do today.

## Decision

### D1 Strict mode is a checking mode, never a code generation mode

`-fcrug-strict` changes diagnostics and nothing else. It implies `-fcrug-ownership`
and `-fcrug-borrow` through the flag closure (ADR 0018 D1), so it enables two
analyses, but the generated object code of a program that strict mode accepts is
byte-identical to the same program compiled with the flags it implies.

Consequences:

- A strict-only diagnostic may never introduce a helper, a flag word, a null check
  or a different calling convention (ADR 0003 D5).
- The gate compares object files (`scripts/check-strict-codegen.sh`) rather than
  inspecting dumps.
- Warnings that exist in hybrid mode keep their codes and their severity mapping:
  strict promotes the documented warnings to errors through the existing option
  machinery, and the promoted diagnostic still carries its `-Wcrug-` option name
  in hybrid mode.

### D2 `raw` is an unchecked pointer, not an ownership state

`raw T *p` declares a pointer with no analysis obligations. It is not owned, it
is not borrowed, it does not alias-track, and it adds nothing to the object
representation: `sizeof (raw T *)== sizeof (T *)`, identical alignment, identical
calling representation (RFC-0001 sections 14 and 22).

Under `-fcrug-strict`, a dereference of a pointer declared `raw` is rejected
outside an `unsafe` block:

    error: dereference of 'raw' pointer 'p' outside an 'unsafe' block [crug-SAFE-001]

The dereference forms covered are `*p`, `p->member` and `p[i]`. The check is on
the declared pointer, including a copy of it: once a `raw` variable holds a plain
pointer value, dereferencing the `raw` variable is what is checked, not the value's
origin. Reads and writes are treated the same.

Placement rules are ADR 0018 D2's: `raw` qualifies the outermost pointer level and
is rejected on a non-pointer declarator (TYPE-001), on a pointer to function
(TYPE-005), on a bit-field, a flexible array member or an unnamed member (TYPE-006),
and combined with another Crug qualifier (TYPE-002).

### D3 Integer-pointer conversion is rejected outside `unsafe` under strict mode

A cast between an integer type and a pointer type is rejected outside an `unsafe`
block:

    error: conversion between an integer and a pointer outside an 'unsafe' block [crug-SAFE-002]

The one exemption is the null pointer constant: an integer constant expression with
value zero, in either direction, is accepted because it cannot manufacture an
address. `(void *) 0` and `0` as a pointer initializer stay legal.

Rationale: the optimized representation of an owned pointer is the pointer itself
(ADR 0003 D1), so an integer round-trip is the one operation that can turn an owned
pointer into a value the analysis cannot follow. Section 21 already says that a
`uintptr_t` round trip is not a borrow, but it does not say whether strict mode
accepts it. This ADR says: only inside `unsafe`, and then with no guarantees.

### D4 An owned value is never converted to an unmanaged pointer

Converting an owned value to a non-owning pointer type is rejected outside an
`unsafe` block, even though ownership is erased at the ABI and the conversion is a
no-op bit pattern:

    error: an owned value cannot be converted to an unmanaged pointer;
           release ('a') first, or use an 'unsafe' block [crug-SAFE-003]

The reason is not representation but obligation. Ownership survives the cast in the
analysis, so a cast that pretends the value is unmanaged either duplicates the
obligation (two owners, one destructor) or hides it (a leak). The two supported ways
to hand a checked value to unchecked code are:

1. `release (x)`, which relinquishes ownership and leaves the variable MOVED
   (ADR 0005); the callee owns whatever the pointer names, with no destructor.
2. `adopt (x, dtor)` in the callee's signature, which is the .crugffi contract
   (Phase 9, ADR 0007).

Inside `unsafe` the cast is accepted and the analysis keeps tracking the variable
as owned, so a later destructor still runs.

### D5 Inline asm is confined to `unsafe`

An `asm` statement outside an `unsafe` block is rejected under strict mode:

    error: an 'asm' statement outside an 'unsafe' block can change memory that
           Crug tracks [crug-SAFE-004]

Extended asm with a memory clobber can read or write any object the analysis tracks,
including an owned pointer's storage, without a visible operation. Strict mode does
not attempt to model asm; it requires the programmer to declare the region
unchecked.

Ownership inside an asm statement is not analysed: operands are treated as uses of
the values they name, no destructor is inserted for them, and no diagnostic about
ownership is emitted for code the asm performs.

### D6 Non-local exits and unchecked calls are strict errors

Three constructs can bypass the destruction facts ADR 0005 established. Strict mode
turns each of them into an error outside `unsafe`:

1. `setjmp`, `_setjmp`, `sigsetjmp`, `__sigsetjmp` and the matching
   `longjmp` family:

        error: 'setjmp' bypasses automatic destruction; call it inside an 'unsafe'
               block under '-fcrug-strict' [crug-SAFE-005]

   The message names the function that was called. In hybrid mode the same situation
   is the ADR 0005 A-6 warning `-Wcrug-setjmp-drops [crug-DROP-003]`, which is
   reported once per owned declaration with a known destructor.

2. Passing an owned value to an `own` parameter without `move`:

        error: an owned value passed to an 'own' parameter must be moved explicitly:
               write move ('x') [crug-OWN-011]

   The parameter owns the value in the callee (ADR 0005 A-1), so the caller must
   give up its own obligation explicitly. An implicit transfer would be a transfer
   the reader cannot see.

3. Passing an owned value to a callee with no ownership contract:

        error: an owned value passed to 'plain' has no ownership contract;
               release ('x') first, or use an 'unsafe' block [crug-OWN-012]

   A known callee whose parameter is neither `own`, `ref` nor `mut` has no
   contract; an unknown callee (no prototype, or a function pointer) has none either.
   The same text is used with "a function without an ownership contract" when the
   callee is not a known declaration.

   The unknown-FFI policy follows from this rule: until Phase 9 attaches `.crugffi`
   metadata (ADR 0007), an owned value crossing to a foreign function is OWN-012
   under strict mode, and the programmer must `release` it or wrap the call in
   `unsafe`. The borrow equivalent was decided in ADR 0019 D2 (BORROW-020) and is
   not a strict-only rule.

Related rule outside strict mode: a discarded `adopt` result is always an error,
because the destructor would be attached to nothing and the object would leak:

    error: the result of 'adopt' is discarded; initialize an owned declaration with
           it [crug-OWN-013]

### D7 What `unsafe` suppresses, and what it does not

An `unsafe { ... }` block suppresses exactly the strict checks in D2, D3, D4, D5 and
D6, and nothing else. It never suppresses:

- use-after-move (UAM-001..004), double drop, implicit copy, partial moves;
- borrow conflicts and escapes (BORROW-001..020);
- the borrow call-site policy (BORROW-020);
- drop-elaboration errors (DROP-001, DROP-002, DROP-006, DROP-007, DROP-008);
- the declaration rules of TYPE-001..007 and the registry rules of section 2.

`unsafe` therefore means "I accept the unchecked operations in this block", not
"this block is unanalysed". A value moved before an `unsafe` block stays moved
inside it.

An `unsafe` block is a scope in this implementation: it is a compound statement, so
a declaration inside it uses the ordinary block rules, including that an owned
variable declared inside is destroyed when the block ends (ADR 0005 D2). The RFC does
not say whether `unsafe` is a scope; this ADR fixes it as one, because the
alternative would make a variable's destruction point depend on whether the
surrounding code is wrapped.

### D8 Hybrid mode is unchanged

Without `-fcrug-strict` every construct in this ADR is accepted (raw dereference,
integer-pointer conversions, owned-to-unmanaged conversions, asm, setjmp/longjmp) and
the only diagnostics are the ones other ADRs already define: the DROP-003 warning for
setjmp, and the ownership and borrow analyses themselves. Hybrid mode is the mode in
which existing C code compiles; strict mode is the mode a new Crug program should be
written in.

### D9 Diagnostic codes

Strict checks carry their own registry area, `SAFE`, so a driver can distinguish a
strict rejection from any other error without parsing English:

| Code | Meaning |
|------|---------|
| `crug-SAFE-001` | dereference of a `raw` pointer outside `unsafe` |
| `crug-SAFE-002` | integer-pointer conversion outside `unsafe` |
| `crug-SAFE-003` | owned value converted to an unmanaged pointer outside `unsafe` |
| `crug-SAFE-004` | asm statement outside `unsafe` |
| `crug-SAFE-005` | setjmp/longjmp family outside `unsafe` |

The call-site codes reuse the ownership area: OWN-011 (own parameter without move),
OWN-012 (no ownership contract) and OWN-013 (discarded adopt). All eight are rows in
`docs/crug/DIAGNOSTICS.md`. They are errors in both hybrid and strict mode; the SAFE
rows are strict-only because the construct they describe is legal in hybrid mode, and
the registry's Strict column records that.

### D10 Test obligations

- `gcc/testsuite/crug.dg/strict/raw-deref.c` must be rejected with SAFE-001 under
  `-fcrug-strict` and accepted without it (gate check 1).
- `gcc/testsuite/crug.dg/strict/clean/` is compiled with `-O2 -fcrug-ownership` and
  `-O2 -fcrug-strict` and the objects must be identical (gate check 2).
- `make check-STRICT_UNSAFE` runs the strict dg suite: one rejection per code
  (SAFE-001..005, OWN-011, OWN-012), the hybrid acceptance counterparts, and two
  dg-do-run programs that exercise an `unsafe` block and a strict-mode destruction.
- `scripts/check-strict-policy.sh` prints the eight codes it observed;
  `scripts/check-strict-raw.sh` prints the hybrid/strict pair.
- `make check-KERNEL_COMPAT` and `make check-C_COMPAT` must keep passing, because
  the strict checks are front-end only and must not disturb plain C or kernel code.

## Consequences

- The strict mode is decidable at the front end: every check is a syntactic or
  local-analysis fact, with no optimizer involvement.
- The unsafe block is the only escape hatch, so every unchecked operation in a
  strict program is textually visible.
- Kernel mode cannot use `-fcrug-strict` yet: the kernel corpus contains asm and
  integer-pointer conversions, and Phase 19 decides whether kernel mode accepts
  `unsafe` or a different policy. Today `-fcrug-kernel` rejects
  `-fcrug-ownership` (and therefore `-fcrug-strict`) outright (ADR 0005 A-7).
- Existing C code that casts integers to pointers for hardware access needs either
  hybrid mode or an `unsafe` block; that is the intended cost of strict mode.

## Alternatives rejected

- Making `raw` dereference a warning in strict mode: rejected because strict mode
  exists to make the unchecked operation explicit, and a warning is routinely
  ignored.
- Suppressing all ownership diagnostics inside `unsafe`: rejected because it would
  turn one escape hatch into a hole in every guarantee the compiler makes, and
  because the difference between "unchecked operation" and "unanalysed code" is
  exactly what a reader needs.
- Treating `unsafe` as a non-scope: rejected for the destruction-point reason in
  D7.
- Allowing owned-to-unmanaged casts silently because the ABI is identical: rejected;
  the obligation is not the representation (D4).
- A separate `-fcrug-raw-checks` flag: rejected; the RFC ties `raw` to strict
  contexts, and a third mode would need its own conformance class.

## Open questions

1. Whether `unsafe` should also suppress OWN-011/012 (currently it does; D7 says it
   does not suppress borrow or drop rules). The implementation suppresses the
   call-contract checks inside `unsafe` because they are strict checks; the text of
   D6 presents them as strict errors, so the two agree, but a reader may expect
   `unsafe` to be about memory, not contracts.
2. Whether kernel mode (Phase 19) reuses `unsafe` or introduces a kernel-specific
   escape.
3. Whether `-fcrug-strict` should be the default for new Crug projects in the
   driver (Phase 3 owns driver defaults).
