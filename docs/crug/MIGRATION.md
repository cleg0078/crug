# Migrating C to Crug with c2crug

Status: **Phase 12 implemented and measured, 2026-10-19.** The tool is `./c2crug`, a Python 3 standard-library script; the report schema, corpus and validator are committed with it. ADR 0023 records the admission rules.

## 1. The c2crug tool

The tool takes a **directory** of C sources, never a single file, and never edits the input. Its usage line is:

```
usage: c2crug [-h] [--analyze | --safe | --diff] [-o OUTPUT] [--report REPORT] [--version] [root]
```

Measured behavior: `./c2crug --version` prints `c2crug 0.1.0` and exits 0; no mode exits 2; a file argument prints `c2crug: not a directory: migration/01-heap.c` and exits 2; `--safe` with an existing `-o` directory prints `c2crug: output directory already exists: <dir>` and exits 2. It walks the directory for `*.c` files, sorts them, and writes nothing except in `--safe` mode.

The tool is not a compiler and needs no build: it parses a deliberately small subset of C shapes with its own tokenizer. That is the design, not a limitation of the corpus: the admission rules in section 3 are exactly the shapes the tokenizer can prove something about. The output is either a report, a transformed copy, or a diff, all deterministic.

## 2. Modes

`--analyze` is the default recommendation for review: it classifies every candidate and applies nothing. Every line ends with `[not applied]`, including the `own` ones, because nothing was written.

`--safe -o DIR` writes a transformed copy of the tree into `DIR`, which must not exist. An entry that was applied prints without the `[not applied]` suffix; rejected entries keep it. The transformation is conservative and textual: insert `own ` before the declaration and replace the release call with `drop (`. The tool does not re-print the source, so formatting, comments and line count are preserved exactly.

`--diff` prints a unified diff against the input and writes nothing. On `migration/` it exits 0 with four changed files (`01-heap.c`, `02-string.c`, `03-file.c`, `07-mixed.c`) and ten changed lines out of 222, which is 4.5% against the 20% ceiling; comment lines stay at 16 because the transformation never removes one.

`--report FILE` works in any mode and is independent of `-o`: `--analyze --report r.json migration/` writes the report and still applies nothing. Output-directory errors and usage errors exit 2; a run that wrote a report or a safe copy exits 0.

## 3. What it proves

For one declaration the tool proves a narrow, checkable statement: under the recorded allocation and release catalogue, this pointer is allocated once, released once on every path, and never copied, escaped, reassigned, returned or used after release. If all of that holds, it may insert `own` and `drop`.

The admission rules are:
1. A single-line declaration of the form `TYPE *name = EXPR;` in a function body.
2. A known origin: `malloc`, `calloc`, `realloc`, `reallocarray`, `strdup`, `strndup`, `aligned_alloc`, or a function declared `returns_owned` by a `.crugffi` sidecar.
3. Exactly one release, a standalone call at the declaration's brace depth; a release inside a brace-less control body does not count.
4. No early `return`, `goto`, `break` or `continue` that skips the release, except a null guard such as `if (!name) return;`.
5. No `&name`, reassignment, pointer arithmetic, `return name`, copy into another variable, escape to an unknown callee, or use after the release.

When every rule holds, the entry is `own` with `confidence=1.00` and is applied by `--safe`. When a rule fails, the entry stays `raw` with `confidence=0.00` and the reason names the failing line; the tool never guesses. Two suggestion kinds are weaker: a parameter never written inside the function yields `borrow-suggestion` with `confidence=0.80` (`ref`), and one written yields `0.60` (`mut`); suggestions are never applied.

What it does not prove is equally important: not that the whole program is equivalent, not that the release function frees the right thing (the catalogue says it does), not that another alias does not exist in code outside the directory, and not that the migrated program compiles without the Crug flags. Those are checked by the corpus test and by compiling with `-fcrug-ownership`.

## 4. The report

The report is the machine-readable artifact. `docs/crug/c2crug-report.schema.json` defines `crug-c2crug-report-v1` (`$id` `https://crug.dev/schema/c2crug-report-v1.json`).

Top-level fields: `schema`, `tool`, `version`, `mode` (`analyze` or `safe`), `sources`, `entries`, `counts`. Each source has `file` and a 64-hex `sha256`; each entry has `file`, `line`, `column`, `kind`, `applied`, `confidence`, `reason`, `evidence` (the source file, line and text that justified the reason), an optional `transformation` (`declaration_line`, `release_line`, `release_from`, `release_to`) and, for a borrow suggestion, `suggestion: "ref"`.

Measured on `--analyze migration/`: 12 entries, counts `{"own": 5, "raw": 5, "borrow_suggestion": 2, "applied": 0}`, schema version 1, mode `analyze`. The validator run is:

```
$ python3 scripts/validate-c2crug-report.py /tmp/crug-analyze.json
c2crug-report: valid (5 own, 5 raw, 2 suggestion(s), 0 applied)
```

The same validator on a `--safe` report reports 5 applied. A report whose `sha256` no longer matches its source, whose mode is unknown, or whose counts disagree with the entries fails validation, so a stale report cannot be attached to a changed tree. Reports are reviewed as data, not prose: a reviewer checks every `own` entry's reason and evidence, not just the summary line.

## 5. The corpus

`migration/` is the eight-program corpus with `migration/expected/*.out` recording each program's stdout. The programs are chosen to cover one admission rule each:

| Source | Classification | What it demonstrates |
|--------|----------------|----------------------|
| `01-heap.c` | own 1.00 | One `malloc` released once by `free` on a straight-line path. |
| `02-string.c` | own 1.00 | The `strdup` catalogue entry and the same release rule. |
| `03-file.c` | own 1.00 twice | `fopen` declared `returns_owned` by `migration/libc.crugffi`, released by `fclose`. |
| `04-branch.c` | raw 0.00 | The release at line 21 is on a conditional path. |
| `05-unknown.c` | raw 0.00 twice | The pointer is returned at line 15; on another path the origin `make_message` is unknown. |
| `06-alias.c` | raw 0.00 | The pointer is copied into another variable at line 12. |
| `07-mixed.c` | own 1.00 and raw 0.00 | One straight-line case beside one conditional release at line 24. |
| `08-params.c` | two borrow suggestions | `values` is only read in `total` (0.80 ref) and written in `fill` (0.60 mut). |

`migration/libc.crugffi` holds the two sidecar forms the tool accepts: `fn NAME returns owned TYPE [nullable] drop DROPFN;` and `fn FN consumes argN;`. Sidecars are hand-written and committed; the tool records and obeys them but never generates one (ADR 0007).

`make -C migration test` is the corpus gate: it builds each original program with the host `cc` and its migrated copy with `crugc`, requires the recorded stdout from both, and prints `migration: 8 programs pass`. That is 8 programs, 5 applied `own` entries, 5 preserved `raw` entries and 2 suggestions, matching the analyze report.

## 6. Worked examples

The heap case, from `./c2crug --analyze migration/`:

```
01-heap.c:14:3: own confidence=1.00 single malloc from the allocation catalogue released once by free on a straight-line path [not applied]
```

and from `./c2crug --diff migration/`:

```diff
-  int *values = malloc ((size_t) n * sizeof (int));
+  own int *values = malloc ((size_t) n * sizeof (int));
...
-  free (values);
+  drop (values);
```

The file case shows the sidecar path: `libc.crugffi` declares `fopen` as `returns_owned` with `drop fclose`, so both `fopen` calls are classified `own`:

```
03-file.c:12:3: own confidence=1.00 single fopen from the .crugffi contract for fopen released once by fclose on a straight-line path [not applied]
03-file.c:26:3: own confidence=1.00 single fopen from the .crugffi contract for fopen released once by fclose on a straight-line path [not applied]
```

The rejections name their evidence, which is what makes review possible without re-deriving the facts:

```
04-branch.c:11:3: raw confidence=0.00 the release at line 21 is on a conditional path [not applied]
05-unknown.c:12:3: raw confidence=0.00 the pointer is returned (line 15) [not applied]
05-unknown.c:21:3: raw confidence=0.00 unknown ownership origin: make_message is neither an allocation function nor declared owned by a sidecar [not applied]
06-alias.c:11:3: raw confidence=0.00 the pointer is copied into another variable (line 12) [not applied]
```

The mixed and parameter cases show why a summary count is not enough:

```
07-mixed.c:11:3: own confidence=1.00 single malloc from the allocation catalogue released once by free on a straight-line path [not applied]
07-mixed.c:12:3: raw confidence=0.00 the release at line 24 is on a conditional path [not applied]
08-params.c:8:11: borrow-suggestion confidence=0.80 parameter values is never written inside total; a ref parameter would state that (suggestion only) [not applied]
08-params.c:17:5: borrow-suggestion confidence=0.60 parameter values is written inside fill; a mut parameter would state that (suggestion only) [not applied]
```

The run summary is one line: `c2crug: 8 file(s), 5 own, 5 raw, 2 borrow suggestion(s)`. Runnable programs with their full source and expected stdout are in `docs/crug/MIGRATION-EXAMPLES.md`, checked by `make check-docs-examples`; this page cites the tool's output, not those programs.

One documentation inconsistency: `docs/crug/C2CRUG.md` section 4's sample line reads `own confidence=1.00 free -> drop [applied]`, but the implemented tool prints the reason prose shown above and never prints an `[applied]` marker; `--safe` suppresses `[not applied]` on applied entries instead. Trust the tool's output, the schema and the tests.

## 7. Limits

The tool is a definition-use analysis over one directory, not a compiler and not a proof. Its limits are deliberate and each one is a place where a human review is still required.

- The declaration must be one line of the accepted shape. Multi-line declarations, several declarators on one line, global variables, struct members, arrays of pointers and function pointers are not classified.
- The catalogues are closed. A custom allocator or deallocator is unknown unless a sidecar declares it; a `realloc` pattern is not treated as an ownership transfer.
- The straight-line rule is strict. A conditional release, an early return, a `goto` or a `break` that skips the release makes the entry `raw` rather than suggesting a fix, and the tool never inserts cleanup code.
- Aliasing `&name`, copies, pointer arithmetic, pass-through to unknown callees and returns all reject the entry. The analysis does not do points-to analysis outside the directory.
- The input must compile as C that the Crug front end accepts once migrated: `own` and `drop` need `-fcrug-ownership`, and a sidecar needs `-fcrug-ffi=`. Without those flags the migrated program does not build; the tool does not add flags to your build system.
- `--safe` never reformats and never rewrites anything except the declaration prefix and the release call, so an unusual but valid release form (extra parentheses, a macro around `free`, a cast) is simply not applied. The diff is meant to be reviewed line by line and kept small.
- The corpus proves eight programs' stdout and the report proves each entry's stated reason. Neither proves the migrated program is equivalent to the original under concurrency, signals or undefined behavior; those remain the job of the test suite described in `docs/crug/TESTING.md`.
- Exit status 2 means the input or invocation was wrong (missing mode, file instead of directory, existing output directory), not that the code was rejected; a corpus of entirely `raw` entries still exits 0.
