# ADR 0010 - Noalias lowering from proven ownership

- **Status:** Accepted
- **Date:** 2026-10-19
- **Deciders:** project owner (the 5% acceptance band, ratification), compiler
  maintainer (the lowering and its audit)
- **Relates:** ADR 0003 (metadata is static, no hidden state), ADR 0004
  (ownership analysis), ADR 0005 (drop lowering), ADR 0019 and ADR 0020 (the
  borrow policies the fact depends on), `docs/crug/BORROW.md` section 2.4,
  `docs/crug/OPTIMIZATION.md`, `docs/crug/EXIT-GATES.md` phase 11

## Context

MASTER_SPEC section 79 reserves this ADR for "noalias lowering", and phase 11
asks for ownership-aware optimization: a `mut` parameter is an exclusive
borrow, so the optimizer may treat it as C `restrict` and vectorize the loop
it could not vectorize through a plain pointer. The specification never says:

1. whether the fact is on by default;
2. which qualifiers prove it (`mut` only, or `own` too);
3. how a caller can prove the contract the fact asserts;
4. what happens when the contract cannot be proven;
5. which performance number closes the phase, and who ratifies it;
6. what must be true for a false fact (a silent miscompilation) to stop the
   phase.

The owner ratified the acceptance band and the benchmark matrix on 2026-10-19:
the Crug arm must be within 5% of a hand-written `restrict` twin, measured on
the three-arm matrix in `bench/`; the band is recorded here so that the phase
gate has a number rather than the word "restrict-like".

## Decision

### D1 - The fact is opt-in

`-fcrug-alias-facts` is off by default. It implies `-fcrug-borrow`, which
implies `-fcrug-ownership`. The reason is that a noalias fact is a
*caller-side contract*: enabling it changes code generation and makes a program
that ignores the contract undefined, exactly as a hand-written `restrict`
does. Existing plain C and existing Crug programs that do not pass the flag
compile to the same object they did before (measured in
`scripts/check-optimization.sh`, section "flag-off").

`-fcrug-modern` does not enable the flag. A future profile may, by an
amendment to this ADR, but no current profile claims it.

### D2 - `mut` and `own` parameters prove the fact; `ref` and plain parameters do not

- A `mut` parameter is an exclusive borrow (ADR 0018 D2, ADR 0019): no other
  live borrow or use of its lender may exist for the duration of the call, so
  the parameter is compiled as if the declarator said `restrict`.
- An `own` parameter receives the only ownership of its value (ADR 0003, ADR
  0004): the caller has transferred it and cannot hold another view, so the
  parameter is compiled as `restrict` too.
- A `ref` parameter is a shared borrow: other shared borrows of the same
  lender may coexist, so no fact is emitted. `scripts/check-optimization.sh`
  proves that a ref-only function is byte-identical to its plain
  `const`-pointer twin.
- A plain pointer proves nothing and gets no fact.

The fact is applied to the outer pointer type of the PARM_DECL. The parameter
type and `DECL_ARG_TYPE` remain the C ABI types; `restrict` is a qualifier
the middle end uses for alias analysis, not part of a calling convention (D5).

### D3 - Only compiler-derived facts are recorded

`-fcrug-dump=alias` writes `alias <function>: parameter <name> restrict
(mut|own)` for every fact the compiler derived. A `restrict` the user wrote
by hand is not in the dump and is not attributed to ownership. The dump is the
audit surface: `scripts/check-stop-conditions.sh` re-reads every dumped
parameter name in the source and requires the declaration itself to say `mut`
or `own`.

### D4 - The caller must prove exclusivity when the flag is on

With `-fcrug-alias-facts`, a call to a function with a `mut` parameter is
rejected unless the argument is a mutable borrow. Six diagnostics enforce the
contract, all errors, all independent of `-fcrug-strict`:

| Code | Condition |
|------|-----------|
| `crug-BORROW-021` | a plain pointer is passed where a `mut` parameter is declared |
| `crug-BORROW-022` | a shared borrow is passed to a `mut` parameter |
| `crug-BORROW-002` | one call passes two borrows of one lender, one shared and one mutable |
| `crug-BORROW-003` | one call passes two mutable borrows of one lender |
| `crug-BORROW-023` | the lender itself (a pointer derived from it) is passed beside its mutable borrow |
| `crug-BORROW-024` | a value that may be the lender was stored where the pass cannot track it, so exclusivity can no longer be proven |

The sixth diagnostic covers escapes the pass cannot follow (a file-scope
object, a structure member, an array element, memory reached through a
pointer): the stored value may be another view, so the fact is withdrawn.

Without the flag the historical leniency of `BORROW.md` section 2.4 stays: a
plain pointer at a `mut` parameter is accepted and no fact is emitted. The
ownership *checker* still enforces the older borrow rules in both modes; the
call-site rules above exist because only the flag turns an unproven argument
into a noalias fact.

### D5 - The ABI does not change

`restrict` is erased in the same way as `own`, `ref`, `mut` and
`raw` (ADR 0003, ADR 0006): the parameter's calling convention, size and
alignment are unchanged, the object files of a C caller and a Crug callee link
without a shim, and no runtime support is used. An owning payload enum is still
refused at a function boundary by value (`crug-ADT-018`), independently of
this ADR.

### D6 - Acceptance is a 5% band with byte identity as the primary evidence

The benchmark matrix (`bench/`) compiles each kernel three ways from one
kernel translation unit: plain C pointers, C + hand-written `restrict`, and
Crug `mut`/`ref` parameters with the flag. The phase gate requires, for
every kernel:

- the Crug kernel object and the hand-written `restrict` kernel object are
  **byte-identical** at `-O2` and `-O3`; and
- the measured Crug/restrict ratio is at most 1.05 whenever the objects are not
  identical.

Identity is the stronger statement and the one the gate actually rests on:
identical machine code cannot be more than 5% slower for a reason attributable
to the fact. Wall-clock timing is still printed, but this host's timing is
noisy enough that the ratio of two identical objects has been observed anywhere
from 0.65 to 2.3; the driver therefore reports 1.000 for identical objects and
measures only when the code differs.

Plain C must not be degraded: `bench/run.py --plain-c --check` compares the
plain arm with `bench/baseline.json` recorded at the phase-11 commit.
Non-LTO levels are enforced through byte identity of the recorded plain kernel
objects; LTO timing is informational because GCC's LTO object files are not
byte-reproducible (two builds of one source differ), so only LTO *correctness*
is asserted.

### D7 - A false noalias is a P0

Two checks exist because a false fact is a silent miscompilation:

- `scripts/check-stop-conditions.sh` audits every dumped fact against the
  source declaration and requires zero false-noalias reports; it also requires
  that a ref-only function has no fact and that at least one fact was audited;
- `scripts/check-optimization.sh` compiles the adversarial fixtures
  (`borrow-021.c`, `borrow-022.c`, `borrow-002.c`, `borrow-003.c`,
  `borrow-023.c`) and requires each rejection, and checks the golden alias
  dump of `noalias-golden.c` against `noalias-golden.expected`.

A failure in either check stops the phase; it is not waived by a passing
benchmark.

### D8 - Non-goals and the limits of the fact

- No fact from unproven ownership: plain pointers, `ref` parameters and
  unannotated C functions never produce one.
- No new GIMPLE opcode, no vectorizer change: the only mechanism is the
  `restrict` qualifier on the parameter type.
- No interprocedural propagation: the fact is local to the callee's parameters.
- No claim that Crug beats hand-written C (RFC section 26): the target is
  parity with the restrict twin, not a speedup over it.
- The contract is not enforced against a caller compiled without the flag. As
  with `restrict`, an unannotated caller can break it; the flag is what makes
  the compiler check the caller.
- Owned payload enums inside aggregates and owned payloads in
  `option`/`result` remain untracked (phase 10 boundary), so no fact is
  derived from them.

## Consequences

- A `mut` parameter now has an observable contract at every call site when
  the flag is on; this is a source-level break for code that passed plain
  pointers to `mut` parameters, which is why the flag is opt-in.
- Three new diagnostics (BORROW-021..023) join the registry; they are errors
  because the alternative is a silent miscompilation.
- The benchmark matrix and its baseline become part of the phase-11 gate, and a
  plain-C codegen change is detected by object identity rather than by a noisy
  timing band.
- `-fcrug-dump=alias` is a stable audit format for this ADR; changing its
  spelling requires an amendment.

## Test paths

- `gcc/testsuite/crug.dg/alias/` (DejaGnu: acceptance, flag-off leniency,
  five rejections);
- `gcc/testsuite/crug.dg/optimization/noalias-golden.{c,expected}` (golden
  dump);
- `bench/` + `bench/run.py` (three-arm matrix, identity, LTO correctness);
- `scripts/check-optimization.sh`, `scripts/check-stop-conditions.sh`.
