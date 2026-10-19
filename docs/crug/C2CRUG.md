# c2crug - migration from C to Crug

**Status:** implemented (Phase 12, 2026-10-19). Decision record: ADR 0023.
Gate: `EXIT-GATES.md` phase 12, amendment A-15. Corpus: `migration/`.

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
`--analyze` resets `applied` on every entry: it is a plan, and the validator
rejects an analyze report that claims an edit.

Exactly one mode is required.  `./c2crug --version` is the exception: it
prints `c2crug <version>` and exits 0 without a source tree, so a build script
can probe the tool before it has a corpus.

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
| `own` | 1.00 | yes |
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
kind, applied, confidence, reason, evidence (`file`/`line`/`text`) and,
for applied entries, the transformation. Runs print:

`
file.c:12:5: own confidence=1.00 free -> drop [applied]
file.c:20:5: raw confidence=0.00 the release at line 22 is on a conditional path
`

## 5. Sidecars (`.crugffi`)

The tool reads every `.crugffi` under the tree. It needs two things:
`fn NAME returns owned TYPE [nullable] drop DROPFN;` teaches it that a call
returns owned storage released by `DROPFN`, and `fn FN consumes argN;` is
accepted (and recorded) so that a release function can also be a consuming call.
`migration/libc.crugffi` is the corpus example for `fopen`/`fclose`. The
tool consumes sidecars; it never generates them (ADR 0007 owns the format).

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
migrated tree with `crugc`; both print
`migration: 8 programs pass`. Measured: 5 `own` applied, 5 `raw`
preserved, 10 of 222 source lines changed (4.5%), comment lines 16 -> 16.

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
| `python3 scripts/test-c2crug.py` | 36 checks over 11 cases: transform, confidence, raw preservation, analyze writes nothing, safe copies, validator lies rejected, command line (`--version` without a tree, a run without a mode exits 2) |
| `scripts/check-c2crug.sh` | the whole flow on `migration/`: analyze, safe, validate, compile with `crugc`, differential run, churn/comment band, report-to-tree agreement |
| `scripts/c2crug-diff.sh SRC OUT [MAX]` | changed lines <= MAX% and comment lines equal |
| `make check-C2CRUG` | both of the first two |
| `./c2crug --analyze migration/ | grep -c raw` | at least one `raw` entry for ambiguous ownership |

The Phase 12 gate asserts exactly these commands; see `EXIT-GATES.md`.
