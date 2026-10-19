# Strict Mode, `raw` and `unsafe`

**Status:** Phase 8 implemented, 2026-10-19. Normative decisions are in
[ADR 0021](adr/0021-strict-mode-policy.md) (Proposed); this page describes the
implemented behaviour and the evidence.

## 1. What strict mode is

`-fcrug-strict` turns the ownership and borrow checkers on (the flag closure:
strict implies ownership and borrow) and adds the restrictions below. It never
changes generated code: a program that strict mode accepts compiles to the same
object bytes as the same program under `-fcrug-ownership` (ADR 0021 D1, checked by
`scripts/check-strict-codegen.sh`).

Strict mode is a *checking* mode. The extra restrictions are about operations that
the analyses cannot follow, not about code quality.

| Restriction | Outside `unsafe` | Inside `unsafe` | Code |
|-------------|-------------------|------------------|------|
| Dereference of a `raw` pointer | rejected | accepted | `crug-SAFE-001` |
| Integer ↔ pointer conversion | rejected | accepted | `crug-SAFE-002` |
| Owned value → unmanaged pointer | rejected | accepted | `crug-SAFE-003` |
| `asm` statement | rejected | accepted | `crug-SAFE-004` |
| `setjmp`/`longjmp` family | rejected | accepted | `crug-SAFE-005` |
| Owned value to an `own` parameter without `move` | rejected | accepted | `crug-OWN-011` |
| Owned value to a callee with no ownership contract | rejected | accepted | `crug-OWN-012` |

The five SAFE rows are legal in hybrid mode: without `-fcrug-strict` a raw
dereference, an integer-pointer cast, an owned-to-unmanaged cast, an `asm` statement
and a `setjmp`/`longjmp` pair are accepted (the setjmp case keeps the `crug-DROP-003`
warning). The two OWN rows are strict-only: hybrid mode passes an owned value to an
`own` parameter, or to a callee with no contract, without a diagnostic.

## 2. `raw` pointers

`raw` declares a pointer with no analysis obligations: it is not owned, not
borrowed, and carries no metadata. The object representation is the plain pointer's
(RFC-0001 sections 14 and 22): same size, alignment, layout and calling
representation, so it can be used in a C prototype without recompiling anything.

```c
struct Foo { int v; };

int read_it (raw struct Foo *p)   /* a plain pointer view, no ownership */
{
  return p->v;                    /* accepted in hybrid mode, rejected under strict */
}
```

Placement rules are the general qualifier rules (ADR 0018 D2): `raw` qualifies the
outermost pointer level, is rejected on a non-pointer declarator (`crug-TYPE-001`),
on a pointer to function (`crug-TYPE-005`), on a bit-field, a flexible array member
or an unnamed member (`crug-TYPE-006`), and combined with another Crug qualifier
(`crug-TYPE-002`).

## 3. `unsafe` blocks

`unsafe { ... }` marks code for which the programmer accepts responsibility. It has
no runtime cost (the block leaves no trace in the object code) and it is a scope: a
declaration inside it follows the ordinary block rules, including destruction at the
block end.

It suppresses exactly the checks in section 1 and nothing else. Inside an `unsafe`
block the ownership, borrow and drop rules still apply: a moved value stays moved, a
borrow conflict is still an error, and a drop that cannot be proven is still rejected.
`unsafe` means "these particular unchecked operations are intended", not "this code
is unanalysed" (ADR 0021 D7).

```c
int read_it (raw struct Foo *p)
{
  int r;
  unsafe { r = p->v; }            /* accepted: the dereference is declared here */
  return r;
}
```

## 4. Ownership at boundaries

Strict mode makes the ownership act at a call site explicit:

- passing an owned value to an `own` parameter requires `move (x)`
  (`crug-OWN-011`), because the callee takes the obligation;
- passing an owned value to any other parameter of a known callee, or to an unknown
  callee, requires `release (x)` first or an `unsafe` block (`crug-OWN-012`);
- `ref` and `mut` parameters are the checked way to lend a value.

Before Phase 9 there is no `.crugffi` reading, so every foreign function is "a callee
without an ownership contract" and an owned value crossing to it is `crug-OWN-012`
under strict mode.

One rule applies in both modes: a discarded `adopt` result is an error
(`crug-OWN-013`), because the destructor would be attached to nothing and the object
would leak. Write `own T *p = adopt (raw, dtor);`, not `adopt (raw, dtor);`.

## 5. What strict mode does not do

- It does not make `unsafe` code memory-safe, and it does not check provenance.
- It does not add a UB sanitizer or any runtime instrumentation.
- It does not give the optimizer facts: `own`, `ref` and `mut` still imply no
  `noalias` (ADR 0003 D8, Phase 11).
- It does not restrict the kernel mode; `-fcrug-kernel` rejects `-fcrug-ownership`
  (and therefore `-fcrug-strict`) until Phase 19 (ADR 0005 A-7).

## 6. Diagnostics

All strict restrictions carry a stable code and are errors in both modes (the SAFE
rows only exist under `-fcrug-strict`). The registry is
[docs/crug/DIAGNOSTICS.md](DIAGNOSTICS.md) sections 4.9 and 5.

```
strict-policy: SAFE-001 SAFE-002 SAFE-003 SAFE-004 SAFE-005 OWN-011 OWN-012
```

## 7. Tests

| Suite | What it covers |
|-------|----------------|
| `gcc/testsuite/crug.dg/strict/` (12 files, 21 DejaGnu tests) | one rejection per code, hybrid acceptance counterparts, two `dg-do run` programs |
| `gcc/testsuite/crug.dg/strict/clean/` (4 programs) | compiled with `-fcrug-ownership` and with `-fcrug-strict` for the byte-identity check |
| `scripts/check-strict-suite.sh` | runs `make check-STRICT_UNSAFE`, prints `strict-suite: 21 tests, 0 failures` |
| `scripts/check-strict-raw.sh` | the same fixture accepted in hybrid mode and rejected with `crug-SAFE-001` under strict |
| `scripts/check-strict-codegen.sh` | objects identical with and without the strict flag |
| `scripts/check-strict-policy.sh` | the eight codes, observed |

## 8. Exit gate

The Phase 8 gate is seven commands; see
[EXIT-GATES.md](EXIT-GATES.md#phase-8--strict-mode-raw-and-unsafe) and amendment A-9,
which replaced the original checks. In short:

```
scripts/check-strict-raw.sh        => strict-raw: hybrid exit 0, strict [crug-SAFE-001]
scripts/check-strict-codegen.sh    => strict-codegen: 4 programs, 0 differences
scripts/check-strict-suite.sh      => strict-suite: 21 tests, 0 failures
scripts/check-strict-policy.sh     => strict-policy: SAFE-001 ... OWN-012
make check-C_COMPAT                => C_COMPAT: ... 0 FAIL
make check-KERNEL_COMPAT           => kernel-compat: 21/21 work items PASS
scripts/check-diagnostics.sh       => all references registered
```

## 9. Examples

- `examples/08-strict-mode.c` - the same raw dereference accepted in hybrid mode and
  rejected with `-fcrug-strict`, plus the `unsafe` fix.
- `examples/09-unsafe-asm.c` - an `asm` statement outside `unsafe` (rejected under
  strict) and inside `unsafe` (accepted).

## 10. Deferred

- kernel-mode escape policy (Phase 19);
- `.crugffi` contracts that would let an owned value cross a foreign call without
  `release` (Phase 9);
- provenance and optimizer facts (Phase 11);
- a strict-mode dump (`-fcrug-dump=strict`); the codes are the observable today.
