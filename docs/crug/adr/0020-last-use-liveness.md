# ADR 0020 - Borrows End at Their Last Use

- **Status:** Accepted (2026-10-19)
- **Ratified:** 2026-10-19 by CLEG0078 (project owner), as the Phase 7 planning decision.
- **Related:** ADR 0003 D2 (no runtime drop flags), ADR 0005 (drop lowering),
  ADR 0019 (interprocedural policy), RFC-0001 section 18.3,
  GAPS section 10 ("if lifetime end cannot be proven, the borrow remains live"),
  docs/crug/BORROW.md.

## Context

Phase 6 ends a borrow with the block that declares it. That is sound but
rejects correct code, and the rejected code is the common pattern:

    own Foo *p = ...;
    ref Foo *a = borrow (p);
    int x = a->v;                 /* last use of a */
    mut Foo *m = borrow_mut (p);  /* legal: a is dead */
    own Foo *q = move (p);        /* legal: a is dead */

Rejecting that pushes users to hand-scope every borrow or to turn the checker
off. The roadmap's Phase 7 asks for non-lexical lifetimes; the GAPS document
already states the soundness rule: a borrow stays live when the end cannot be
proven.

## Decision

**D1. A borrow's lifetime is its liveness.** It begins at its declaration and
ends after the last statement that reads it. The conflict checks (shared vs
mutable, move, drop, release, assignment to the owner) read the set of borrows
live at that statement, not every borrow in the enclosing blocks.

**D2. Equation.** For each statement `S` in the structured GENERIC body:

    in(S)  = use(S) | (out(S) - def(S))
    out(S) = in(next statements), unioned at every join

`use` is the set of borrow bindings read anywhere in the statement, found by a
tree walk (not by an operand-class walk: `(void) r` is a `NOP_EXPR` and was
missed by the first implementation). `def` is the declared binding of a
`DECL_EXPR` and an assignment target that is itself a borrow.

**D3. Joins union.** If either arm of a conditional reads the borrow, it is
live after the join. This is the conservative direction required by GAPS
section 10: an unproven end means the borrow stays live, so a program that is
accepted here cannot rely on a dead borrow that is in fact used on another
path.

**D4. Loops use a conservative rule, not a fixpoint.** Inside any loop body,
every borrow declared anywhere in the function is treated as live. A loop is
solved once with that widened out-set. This keeps the analysis a single
backward pass with no iteration to a fixpoint and no chance of a
non-converging approximation; the price is that a borrow whose last use is
just before a loop is still live inside the loop. The loop's live-out set is
not widened, so a borrow that is dead after the loop stays dead there.

**D5. Unmodelled control flow abandons liveness.** If the body contains a
`goto` (`liveness_failed`), the analysis does not run and the checker falls
back to the Phase 6 lexical rule for the whole function. Over-reporting is
acceptable; silently ending a live borrow is not.

**D6. The dump distinguishes the two ends.** `-fcrug-dump=borrow` writes
`end <file>:<line>: <name> shares|excludes <lender> (last-use)` for a last-use
end and the same line without the reason for a scope end. The gate asserts that
both kinds appear.

## What this forbids

- Relying on a borrow being live after its last read. It is not: the owner may
  be moved, dropped, released or mutably borrowed at that point.
- Ending a borrow at a statement that does not read it. Only a read extends a
  borrow's life.
- Any analysis that would need a fixpoint in a loop. The conservative rule is
  the model; a smarter rule needs a new ADR and a proof of soundness.

## Consequences

- The NLL programs (`b10`-`b16`) compile and run; the Phase 6 rejections
  still reject because their fixtures read the borrow after the conflicting
  statement (that is what makes them alive there).
- `drop`/`release`/`move` become usable in the natural order: use the borrow,
  then dispose of the owner.
- The checker stays a backward pass over the structured body, so it has no CFG
  and no IR change (ADR 0003 D1: decisions are static facts).

## Evidence

- `gcc/testsuite/crug.dg/borrow/b10-last-use-shared.c` ... `b16-early-return-borrow.c`
  (dg-do run), `r23-live-across-join.c` (still rejected across a join),
  `r24-mut-then-shared.c` (mut borrow still live).
- `scripts/check-borrow-nll.sh` requires both a `(last-use)` end and a scope end
  in the dump.
- `make check-BORROW`: 88 tests, 0 failures at the Phase 7 commit.

## Alternatives rejected

- **Lexical lifetimes only (Phase 6 rule).** Rejected by the roadmap's Phase 7
  and by user experience: it rejects the canonical use-then-dispose order.
- **A real CFG with a liveness fixpoint.** Rejected for this phase: the
  structured body already carries the joins and loops, the conservative loop
  rule is sound, and a CFG would duplicate the checking walk (the two could
  disagree about what a statement is). A later ADR may replace this if the
  false positives matter.
- **Killing a borrow at any statement that does not mention it.** Rejected:
  unsound at joins and in loops.
