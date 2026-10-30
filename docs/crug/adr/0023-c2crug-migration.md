# ADR 0023 - c2crug migration tool: conservative, explicit, never invented

- **Status:** Accepted
- **Date:** 2026-10-19
- **Deciders:** project owner (acceptance of the migration corpus and the
  "largely preserved" reading), compiler maintainer (the proof rules and the
  report schema)
- **Relates:** ADR 0003 (metadata is static, no hidden state), ADR 0004
  (ownership analysis and adoption), ADR 0005 (drop lowering), ADR 0007
  (.crugffi sidecars), RFC-0001 section 67, CRUG_PRODUCTION_ROADMAP section 17,
  docs/crug/C2CRUG.md, docs/crug/EXIT-GATES.md phase 12

## Context

MASTER_SPEC section 3894 says c2crug is specified separately, and the repository
contains no such specification: only RFC-0001 section 67 (the tool is a
migration aid, not the semantic authority; safe mode MUST NOT convert uncertain
ownership into a stronger guarantee) and CRUG_PRODUCTION_ROADMAP section 17
(modes, confidence, no invented safety, acceptance criteria). The specification
never says:

1. which syntactic facts are enough to insert `own`;
2. what happens to everything the tool cannot prove;
3. what a confidence value means and who may act on it;
4. what the machine-readable report must contain;
5. which corpus closes the phase, and what "comments and structure largely
   preserved" means as a number;
6. what the tool may claim about a migrated program.

The exit gate as written names `corpus/` and `make -C out test`. The
repository's `corpus/` is thirty compile-only fragments with no Makefile, no
tests, no output expectations and not a single allocation call, so it cannot
serve as a migration corpus or as its own green test suite. Decision D5 replaces
it with `migration/` and records the change as amendment A-15 in
`docs/crug/EXIT-GATES.md`.

## Decision

### D1 - Safe mode proves ownership structurally, never semantically

`c2crug --safe` inserts `own` before a pointer declaration only when all of
the following hold in one function body:

- the initializer is a known allocating call (`malloc`, `calloc`,
  `realloc`, `reallocarray`, `strdup`, `strndup`, `aligned_alloc`) or a
  function declared `returns owned` in a `.crugffi` sidecar under the tree;
- exactly one release exists for the variable (`free` for the catalogue, the
  declared `drop` function for the sidecar), it is a standalone statement, and
  it sits at the same brace depth as the declaration and not as the body of a
  brace-less `if`/`for`/`while`;
- no early `return`/`goto`/`break`/`continue` between declaration and
  release except a null-guard exit (`if (!p) return ...` or the equivalent);
- there is no address-of, reassignment, arithmetic, `return` of the pointer,
  copy into another variable, escape into an unknown callee, or use after
  release.

Everything else is `raw`. The proof is deliberately shallow: it is a decision
procedure over tokens and brace depth, not an alias or escape analysis.

### D2 - Three modes, none of which overwrites its input

`--analyze` prints findings and writes nothing. `--safe -o OUT` copies the
tree, transforms only the proven declarations and refuses an `OUT` that already
exists (exit 2), so a migration is never applied twice. `--diff` prints the
unified diff and writes nothing. All modes preserve line count: the only edits
are an inserted `own ` prefix and the replacement of the release function name
with `drop`.

Since fix-p3 (2026-10-30) the `--safe` copy is published atomically: the tree is
built under a hidden temporary sibling of `OUT` and renamed into place together
with the report, so a failed run leaves neither a partial tree nor a report.
The same change refuses a source that contains `#line` directives as
unmappable, widens the exit-call set to `_Exit`, `siglongjmp`, `pthread_exit`,
`pthread_cancel` and `thrd_exit`, and treats a file-local `noreturn` function as
an exit.

### D3 - Confidence measures the proof, not the program

An applied `own` has confidence 1.00 because the proof is complete for the
inserted annotation; `raw` has 0.0 because no decision was made. A borrow
suggestion is `ref` (0.80, the pointer is never written) or `mut` (0.60, it
is written through), is marked `applied: false`, and is never applied in any
mode: borrow inference beyond suggestions is a non-goal of the phase.

### D4 - The report is the interface

`--report FILE` writes the JSON document defined by
`docs/crug/c2crug-report.schema.json`: schema id
`crug-c2crug-report-v1`, tool, version, mode, per-file SHA-256, and one entry
per finding with file, line, column, kind, applied, confidence, reason, an
evidence line copied from the source, an optional suggestion and, for an applied
entry, the transformation (`declaration_line`, `release_line`,
`release_from`, `release_to`). The validator
(`scripts/validate-c2crug-report.py`) is independent of the tool: it checks the
schema, the mode invariants (analyze has zero applied entries), and the rule that
`raw` is never applied. A report that lies about the tree is rejected.

### D5 - The corpus is `migration/` at a pinned commit

The phase-12 prerequisite requires "a C corpus project with its own green test
suite at a pinned commit". `corpus/` is compile-only, so `migration/` provides
eight runnable programs, expected standard output for each, a `Makefile` with
`make test`, and a minimal `libc.crugffi` that declares `fopen` as
`returns owned ... drop fclose`. Its pinned commit is the commit at which the
phase-12 gate is archived; the gate's scripts read the corpus from the working
tree, so the pin is the gate record.

### D6 - No invented safety

A migrated declaration compiles and produces the same output; that is the whole
claim. The tool never says the program is memory-safe, never introduces a
destructor where the source freed conditionally, and never turns an ambiguous
case into `own`: ambiguous ownership is preserved verbatim as `raw` with a
report entry naming the line that defeated the proof. The corpus asserts both
halves: five `own` applied, five `raw` preserved, and every non-applied entry
byte-identical to its source line.

### D7 - Limits are declared, not discovered

The tool does not handle: owning pointers in aggregates or arrays, ownership
transferred through a callee, pointer arithmetic that changes the base,
conditional or multiple releases, allocation through a wrapper the sidecar does
not declare, file-scope owning pointers, or owning payload enums. `.crugffi`
files are consumed, never generated. The tool is Python 3 standard library only
and does not change `crugc`.

### D8 - Non-goals and stop conditions

Non-goals: whole-program migration, borrow inference applied automatically,
FFI sidecar generation, and any claim stronger than "this annotation is proven
for this function body". Stop conditions: if a proven `own` fails to compile
under `crugc -fcrug-ownership -fcrug-borrow`, or if a migrated program changes
output, that is a P0 tool defect and stops the phase; the gate checks both.

## Consequences

- `docs/crug/C2CRUG.md` is the user-facing specification, this ADR the
  decision record, and `docs/crug/EXIT-GATES.md` phase 12 the enforcement.
- Phase 12 can be adjudicated mechanically: `scripts/check-c2crug.sh` runs the
  whole flow and prints the counted result; `make check-C2CRUG` runs it
  together with `scripts/test-c2crug.py`.
- A future whole-program or borrow-inference tool would be a new ADR; it must not
  widen this tool's claim by editing it.
