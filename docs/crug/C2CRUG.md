# c2crug - migration from C to Crug

**Status:** implemented (Phase 12, 2026-10-19); preprocessor-aware `--safe` added
2026-10-28; atomic publication and honest `#line` degradation added in fix-p3
(2026-10-30). Decision record: ADR 0023. Gate: `EXIT-GATES.md` phase 12,
amendment A-15. Corpus: `migration/`.

`c2crug` is a Python 3 (standard library only) tool at the repository root. It
reads C, and writes a Crug migration that is *provably* equivalent for the
annotations it inserts. It never invents safety: a pointer whose ownership it
cannot prove is preserved byte for byte and reported as `raw`.

## 1. Modes

| Mode | Command | Writes |
|------|---------|--------|
| analyze | `./c2crug --analyze PATH` | nothing (prints findings) |
| safe | `./c2crug --safe PATH -o OUT` | a copy of PATH under OUT with the proven edits applied |
| diff | `./c2crug --diff PATH` | nothing (prints a unified diff) |

`--report FILE` writes the JSON report in any mode. `--safe` refuses an
`OUT` that already exists (exit 2) so a migration can never be applied twice.
It builds the copy under a hidden temporary sibling of `OUT` and renames it
into place together with the report, so a failed or interrupted run leaves
neither a partial tree nor a report.
`--analyze` resets `applied` on every entry: it is a plan, and the validator
rejects an analyze report that claims an edit.

Exactly one mode is required.  `./c2crug --version` is the exception: it
prints `c2crug <version>` and exits 0 without a source tree, so a build script
can probe the tool before it has a corpus.

`--cpp PATH` selects the preprocessor (`cc` by default) and `--cpp-arg ARG`
adds an argument, repeatable. The tool preprocesses function bodies with
`-E -x c` and maps the preprocessor's `#line` markers back to source lines; a
source that itself contains `#line` directives is refused as unmappable. This
affects eligibility only and never writes the preprocessed text to the output
tree.

## 2. What `--safe` inserts and rewrites

For a variable whose ownership is proven (section 3):

`c
    char *text = strdup ("hello");     ->      own char *text = strdup ("hello");
    ...
    free (text);                       ->      drop (text);
`

Two line-local edits: `own ` is inserted at the declaration's indentation, and
the release function name is replaced with `drop` on the release line. The line
count never changes, so comments, braces and structure survive untouched. Nothing
else in the file is edited, including `raw` declarations.

`drop` requires `-fcrug-ownership`; a sidecar-derived release function is
handled the same way (`fclose` becomes `drop`, and the sidecar is passed with
`-fcrug-ffi=`).

A body is rewrite-eligible only when its declaration prefix and release span
are token-identical to the preprocessed body and preprocessing adds or removes
no exit token. An exit token is a call to a known terminating function
(`exit`, `_exit`, `_Exit`, `abort`, `quick_exit`, `longjmp`, `siglongjmp`,
`pthread_exit`, `pthread_cancel`, `thrd_exit`) or to a function declared
`noreturn` in the same file. Otherwise the declaration stays `raw` at
confidence 0, with one of these reasons in the report: the function body could
not be preprocessed, the source uses `#line` directives so the preprocessed
body cannot be mapped back, a preprocessor directive inside the function body,
or macro expansion inside the function body.

## 3. The proof

A declaration is proven when it is a single-line `TYPE *name = EXPR;` inside a
function body and all of the following hold:

1. **Known origin.** `EXPR` is a direct call to `malloc`, `calloc`,
   `realloc`, `reallocarray`, `strdup`, `strndup` or `aligned_alloc`,
   or to a function a `.crugffi` sidecar declares `returns owned` (its
   `drop FN` names the release).
2. **Exactly one release.** One `REL (name);` for the variable, a standalone
   statement, at the declaration's brace depth, and not the body of a brace-less
   control statement. `if (c) free (p);` is not standalone.
3. **No early escape.** No `return`, `goto`, `break` or `continue`
   between declaration and release that would skip the release, except a
   null-guard exit (`if (!p) return ...;` and equivalents).
4. **The pointer is not manipulated.** No `&name`, no reassignment, no pointer
   arithmetic, no `return name`, no copy into another variable, no escape into
   a callee that is not in the read-only library set, and no use after release.

Anything else is `raw` with a reason naming the line that defeated the proof.
This is a decision procedure over tokens and brace depth, not an escape
analysis; it is conservative in the one direction that matters.

## 4. Confidence and the report

| Kind | Confidence | Applied |
|------|-----------|---------|
| `own`, straight-line release | 1.00 | yes |
| `own`, release after a proven null guard | 0.80 | yes |
| `raw` | 0.00 | never |
| `borrow-suggestion` `ref` | 0.80 | never |
| `borrow-suggestion` `mut` | 0.60 | never |

`ref` means the pointer parameter is never written; `mut` means it is written
through (`*p ==`, `p->f ==`, `p[i] ==`). Suggestions are advice for a human,
and no mode applies them.

The report (`--report FILE`) is validated against
`docs/crug/c2crug-report.schema.json` by
`scripts/validate-c2crug-report.py`, an independent checker: schema id
`crug-c2crug-report-v1`, per-file SHA-256, and per-entry file, line, column,
kind, applied, confidence, reason, evidence (`file`/`line`/`text`),
`preprocessed` (bool), `eligibility` (`eligible` or `ineligible`), and,
for applied entries, the transformation. Runs print:

`
03-file.c:12:3: own confidence=0.80 single fopen from the .crugffi contract for fopen released once by fclose after a proven null guard [not applied]
03-file.c:26:3: own confidence=0.80 single fopen from the .crugffi contract for fopen released once by fclose after a proven null guard [not applied]
witnesses/same-line-exits.c:13:3: raw confidence=0.00 an early exit at line 15 can skip the release [not applied]
`

The two null-guard entries are the `03-file.c` case: the guard proves 0.80, not
1.00. The four witnesses under `migration/witnesses/` each keep their owning
pointer `raw` at confidence 0; `hidden-alias.c`, `nested-return.c` and
`free-redefinition.c` are ineligible after macro expansion, while
`same-line-exits.c` is eligible but raw on the early exit.

`--analyze` prints one line per candidate and never applies an edit, so every
line ends `[not applied]`; a report written by `--safe` marks the entries it
transformed with `applied: true` and records the transformation.

## 5. Sidecars (`.crugffi`)

The tool reads every `.crugffi` under the tree. It needs two things:
`fn NAME returns owned TYPE [nullable] drop DROPFN;` teaches it that a call
returns owned storage released by `DROPFN`, and `fn FN consumes argN;` is
accepted (and recorded) so that a release function can also be a consuming call.
`migration/libc.crugffi` is the corpus example for `fopen`/`fclose`. A sidecar
is read only when its first non-empty line is a `crugffi 1.<minor>;` header
with major version exactly 1; otherwise the tool prints
`c2crug: ignoring sidecar PATH: the first line is not a 'crugffi 1.<minor>;' header`
and continues without it. The tool consumes sidecars; it never generates them
(ADR 0007 owns the format).

## 6. The corpus

`migration/` is a small C project with its own green test suite:

| Program | Ownership outcome |
|---------|-------------------|
| `01-heap.c` | one `own` (malloc/free) |
| `02-string.c` | one `own` (strdup/free) |
| `03-file.c` | two `own` (fopen/fclose via the sidecar) |
| `04-branch.c` | `raw`: the release is conditional |
| `05-unknown.c` | `raw`: unknown origin, and the pointer is returned |
| `06-alias.c` | `raw`: the pointer is copied into another variable |
| `07-mixed.c` | one `own` and one `raw` in one function |
| `08-params.c` | two borrow suggestions (`ref`, `mut`), nothing applied |

`make -C migration test` builds the original corpus with `cc` and diffs each
program's output against `migration/expected/*.out`. The same target builds the
migrated tree with `crugc`; both print `migration: 8 programs pass`. The eight
programs yield 5 `own`, 5 `raw` and 2 borrow suggestions.

Measured on the whole `migration/` tree (20 sources, including `proofs/` and
`witnesses/`): 25 entries, 6 `own`, 16 `raw`, 3 suggestions; `--safe` applies
six entries and `--diff` reports 5 changed files and 12 of 491 source lines
(2.4%).

## 7. Limits

Not handled: owning pointers in aggregates or arrays, ownership transferred
through a callee, pointer arithmetic that changes the base, conditional or
multiple releases, allocation through an undeclared wrapper, file-scope owning
pointers, and owning payload enums. The tool does not change `crugc` and makes
no whole-program claim. A pointer it cannot prove is never annotated: the
migrated program still compiles as plain C and still frees manually.

## 8. Tests

| Check | Result |
|-------|--------|
| `python3 scripts/test-c2crug.py` | 140 checks over 30 cases: transform, confidence, raw preservation, analyze writes nothing, safe copies, validator lies rejected, command line (`--version` without a tree, a run without a mode exits 2), preprocessing eligibility, sidecar headers, report fields |
| `scripts/check-c2crug.sh` | the whole flow on `migration/`: analyze, safe, validate, compile with `crugc`, differential run, churn/comment band, report-to-tree agreement |
| `scripts/c2crug-diff.sh SRC OUT [MAX]` | changed lines <= MAX% and comment lines equal |
| `make check-C2CRUG` | both of the first two |
| `./c2crug --analyze migration/ | grep -c raw` | at least one `raw` entry for ambiguous ownership |

The Phase 12 gate asserts exactly these commands; see `EXIT-GATES.md`.

## 9. Post-rc1 proof rules (2026-10-26)

The proof is now narrower and the report says so. Only a straight-line
unconditional free proves safety at confidence 1.00. A null guard proves 0.80
only when it directly controls an early exit that dominates the release; a
guard anywhere else proves nothing and the declaration stays raw. No transform
is applied without a proof, so a file that cannot be proven is left alone
rather than half-rewritten. Multi-declarator top-level declarations stay raw.
The corpus result is 6 `own` and 16 `raw` declarations with 3 borrow
suggestions over 20 sources; the safe run applies six entries in five files and
`--diff` changes 12 of 491 lines (2.4%). The proof fixtures live in
`migration/proofs/` and `scripts/check-c2crug-proofs.sh` runs them; the four
`migration/witnesses/` regressions stay raw as described in section 4, and
`scripts/test-c2crug.py` covers 140 checks over 30 cases, including the
negative cases (a conditional free, an unrelated guard, a multi-declarator,
preprocessor ineligibility).
