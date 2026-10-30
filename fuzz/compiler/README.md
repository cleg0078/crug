# Compiler fuzz harness (Phase 22)

Deterministic, stdlib-only mutational fuzzer for the Crug compiler front end.
It mutates small seed programs taken from `gcc/testsuite/crug.dg`, compiles
each mutant with the Crug flags of one target, and reports anything that
crashes or hangs the compiler.

## Targets

| `--target` | Compiler flags | What it stresses |
| --- | --- | --- |
| `parser` | `-fcrug-ownership` | The contextual Crug parser: `own`/`ref`/`mut`/`move` and the `go`/`scope`/`chan`/`channel`/`send`/`recv`/`select` spellings, declarators, error recovery. Seeds include words that are Crug keywords only in certain positions, and concurrency syntax that the ownership-only target must reject cleanly. |
| `ownership` | `-fcrug-ownership` | Move, use-after-move, implicit copy, adopt/release/drop and branch-join ownership checking. |
| `borrow` | `-fcrug-borrow` | Shared/mutable borrow rules, reborrows, last-use and borrow-of-rvalue diagnostics. |
| `ffi` | `-fcrug-ffi=<sidecar>` | The `.crugffi` contract parser/checker. The mutated input is the sidecar; a fixed driver `ffi_driver.c` includes a fixed `ffi_api.h` and calls the declared functions through the contract. |

`--check` runs all four targets (1200 inputs at the default budget).

## Requirements

* Python 3 (standard library only; no third-party packages).
* A built compiler driver. The default is `gcc/crugc` at the repository
  root; the harness invokes it as a normal subprocess and does not touch
  the build.

## Usage

```sh
python3 fuzz/compiler/fuzz.py --check                 # all four, default 300 each
python3 fuzz/compiler/fuzz.py --target borrow --budget 1000
FUZZ_BUDGET=2000 python3 fuzz/compiler/fuzz.py --check
python3 fuzz/compiler/fuzz.py --target ffi --seed 7 --findings /tmp/findings
```

Options (defaults in parentheses):

* `--target parser|ownership|borrow|ffi` - fuzz one target (default: none).
* `--check` - fuzz all four targets. Implied when `--target` is omitted.
* `--budget N` - inputs per target (`$FUZZ_BUDGET`, else 300). `0` is a
  valid smoke budget.
* `--seed S` - PRNG seed (20261020).
* `--compiler PATH` - compiler driver (`<root>/gcc/crugc`).
* `--corpus DIR` - corpus root (`fuzz/compiler/corpus`).
* `--findings DIR` - findings root (`<root>/build/fuzz/findings`).

The harness is deterministic: the corpus is read in sorted order and all
randomness comes from `random.Random("<seed>:<target>")`, so the same seed,
corpus and budget produce the same mutants and the same output lines
(finding signatures redact temporary paths, absolute paths and hex
addresses).

## Classification

Each mutant is compiled with

```
<compiler> -fsyntax-only -fno-diagnostics-color [target flags] <input>
```

with a 10-second timeout. The result is:

* **crash** - the process died from a signal (negative return code, e.g.
  `SIGSEGV`) or stderr contains `internal compiler error` or
  `Segmentation fault`;
* **timeout** - the run exceeded 10 s and was killed (process group
  `SIGKILL`);
* **ok** - any other result, including ordinary Crug rejections. Mutants
  are expected to be rejected often; a nonzero exit status is not a
  finding by itself.

Crashes/timeouts are counted per input. Their signatures (signal or matched
message plus the normalized first meaningful stderr line) are deduplicated
within a run, so one bug does not produce hundreds of files.

Output per target and a final summary:

```
fuzz: parser: 300 input(s), 0 crash(es), 0 timeout(s)
...
compiler-fuzz: 4 harness(es), 1200 input(s), 0 crash(es), 0 timeout(s)
compiler-fuzz: OK
```

Exit status: `0` clean, `1` at least one crash/timeout, `2` usage or
setup error (e.g. missing compiler or corpus).

## Findings

A unique finding for target `T` is written to
`<findings>/T/<16-hex-hash>.*`:

* plain targets: `<hash>.c` (the exact input);
* `ffi`: `<hash>.c` (the fixed driver), `<hash>.h` (the fixed API
  header) and `<hash>.crugffi` (the mutated sidecar);
* `<hash>.log`: target, kind, signature, exit status, a copy-pasteable
  reproduce command and an excerpt of stderr;
* `<findings>/T/crashes.log`: one appended line per saved finding.

The findings directory is not cleaned between runs; delete it (or point
`--findings` elsewhere) before a clean gate run. The directory is under
`build/`, which is git-ignored, so findings are never committed.

## Corpus

`corpus/<target>/` holds the seed files; the harness reads every file with
the target's suffix (`.c`, or `.crugffi` for `ffi`) in sorted order and
picks them round-robin, drawing the splice partner at random. Seeds are
adapted byte-for-byte from `gcc/testsuite/crug.dg/` and deliberately mix
valid programs with programs that Crug legitimately rejects. Current
counts: parser 14, ownership 10, borrow 11, ffi 12.

Each mutant is the seed plus 1-4 of:

* byte splice from another corpus file;
* line duplication / line deletion;
* token replacement or token swap using the C/Crug dictionary (and the
  `.crugffi` clause dictionary for `ffi`);
* truncation; insertion of a dictionary token.

Mutants are capped at 8 KiB.

To add a seed, copy a small, self-contained program (ideally one fixture
per grammar/checking feature) into the right directory:

```sh
cp gcc/testsuite/crug.dg/ownership/double-move.c fuzz/compiler/corpus/ownership/
```

* Use the directory matching the flags the fixture needs
  (`-fcrug-ownership` -> parser/ownership, `-fcrug-borrow` -> borrow,
  `.crugffi` -> ffi).
* The name is a stable part of the deterministic stream: adding, renaming
  or removing a seed changes which mutants a given seed produces (still
  reproducible, just different).
* Keep the file under 8 KiB; oversized seeds are truncated on load.
* No build step or manifest is needed.
