# Crug feature flags and feature macros

Status: **Phase 3 implemented; Phases 4, 5 and 6 use it** (2026-10-19). The
option set, its closure and the feature macros exist. `-fcrug-ownership` gates
the `own`, `move` and drop productions of Phases 4 and 5 (`OWNERSHIP.md`,
`DROP.md`), and `-fcrug-borrow` gates `ref`, `mut`, `borrow` and `borrow_mut`
(`BORROW.md`); the productions the other flags name do not exist yet. This page
is the digest - the normative text is ADR 0018 D1 and D9, RFC-0001 sections 10
and 13, and the gates are `EXIT-GATES.md` phases 3, 4, 5 and 6.

## 1. The option set

Ten options are declared in `gcc/common.opt`, all `Common` so that the driver
and every front end accept them, and all with a `Var(...)` so the closure is
computed in one place (`gcc/c-family/crug-common.cc`).

| Option | Enables, when its phase lands | Implies |
| --- | --- | --- |
| `-fcrug-ownership` | `own`, `raw`; `move`, `drop`, `adopt`, `release`; `unsafe {}` | - |
| `-fcrug-borrow` | `ref`, `mut`; `borrow`, `borrow_mut` | `-fcrug-ownership` |
| `-fcrug-slices` | `slice<T>` | - |
| `-fcrug-result` | `option<T>`, `result<T,E>`, `some`, `none`, `ok`, `err` | `-fcrug-adt` |
| `-fcrug-adt` | payload enums, `match` | - |
| `-fcrug-goroutines` | `go`, `scope` | - |
| `-fcrug-channels` | `chan T`; `send`, `recv`, `close`, `channel`; `select` | `-fcrug-goroutines` |
| `-fcrug-strict` | no production; promotes every `-Wcrug-*` warning to an error | `-fcrug-ownership`, `-fcrug-borrow` |
| `-fcrug-modern` | everything above | all eight flags |
| `-fcrug-kernel` | no production; kernel mode | excludes `-fcrug-goroutines`, `-fcrug-channels`, `-fcrug-modern`; rejects `-fcrug-ownership` and `-fcrug-borrow` until Phase 19 |

`-fcrug-dump=<kind>` is a debugging option, not a feature flag: it is
`Common Joined RejectNegative` with a comma-separated list of kinds -
`ownership`, `drop` and `borrow` today; an unknown kind is an error. It writes
`<stem>.crug-ownership`, `<stem>.crug-drop` or `<stem>.crug-borrow` next to the
output base while the front end runs, and changes no code. `OWNERSHIP.md`
section 4, `DROP.md` section 6 and `BORROW.md` section 4 give the formats.

With no option at all, `crugc` is upstream GCC: the same predefined macros, the
same parses, the same object code. That is the Phase 1 identity and it keeps
holding as each feature is added, because each production is reached only after
its flag is on.

## 2. Closure

The C front end closes the set over the implications above and never errors
because a prerequisite was written implicitly: `-fcrug-borrow` alone means
`-fcrug-borrow -fcrug-ownership`. Closure runs once per compilation, in
`c_common_post_options`, before any front-end parsing, and the result is the bit
mask `crug_features`. The driver only forwards the options; it adds no default
of its own, which is what keeps `crugc` argument-transparent (DRIVER.md).

`-fcrug-modern` is the closure of the eight feature flags. It deliberately does
not include `-fcrug-kernel`: the two are contradictory, so asking for both is a
hard error naming the offending flags rather than a precedence rule. The same
error covers `-fcrug-kernel -fcrug-goroutines` and `-fcrug-kernel
-fcrug-channels`.

`-fcrug-kernel` with a compatible flag (`strict`, `slices`, `result`, `adt`)
is accepted: the kernel may use the sublanguage, it may not use the runtime
features. `-fcrug-ownership` and `-fcrug-borrow` are rejected with the kernel
flag because destruction lowering is the first feature that would need a
decision inside the kernel (Phase 5 note; Phase 19 revisits it and the
restriction is recorded in ADR 0005 A-6).

## 3. Feature macros

Defined in `c_cpp_builtins` only when `crug_features` is non-zero:

| Macro | Defined when |
| --- | --- |
| `__CRUG__` | any `-fcrug-*` option is active |
| `__CRUG_COMPILER__` | same |
| `__CRUG_OWNERSHIP__` | `-fcrug-ownership` after closure |
| `__CRUG_BORROW__` | `-fcrug-borrow` after closure |
| `__CRUG_SLICES__` | `-fcrug-slices` |
| `__CRUG_RESULT__` | `-fcrug-result` |
| `__CRUG_ADT__` | `-fcrug-adt` after closure |
| `__CRUG_GOROUTINES__` | `-fcrug-goroutines` |
| `__CRUG_CHANNELS__` | `-fcrug-channels` |
| `__CRUG_STRICT__` | `-fcrug-strict` |
| `__CRUG_KERNEL__` | `-fcrug-kernel` |

Every value is `1`. The macros are *defined*, never `0`, so `#ifdef` and
`#if __CRUG_CHANNELS__` agree.

This is ADR 0018 D9 R6 as amended by A-1: an earlier draft said `__CRUG__` was
predefined always, which would have broken the flags-off identity for plain C
(the Phase 1 gate compares object code, but class C0 compares the token stream
too). With no `-fcrug-*` option the predefined set is byte-identical to the
baseline compiler's, and `scripts/check-crug-flags.sh` asserts that by
comparing `-dM -E` output.

Version macros (`__CRUG_VERSION_MAJOR__`, `__CRUG_VERSION_MINOR__`,
`__CRUG_VERSION_PATCH__`, `__CRUG_RUNTIME_ABI__`, `__CRUG_FFI_VERSION__`) are
**not** defined yet; RFC-0001 section 10 asks for them, ADR 0018 defers them to
ADR 0013 (versioning), and Phase 3 has no version number to report.

## 4. What is guaranteed

- **Words stay identifiers.** `gcc/testsuite/crug.dg/flags/words-as-identifiers.c`
  declares every contextual word as an ordinary C identifier and must compile
  under every accepted combination of flags. With the flags off the parse is
  upstream C; with a flag on, each production still requires an undeclared word
  in a specific position (ADR 0018 D1 R1), so the fixture stays the regression
  test that stops a later phase from reserving a word globally.
- **Codegen is untouched.** `scripts/diff-obj.sh corpus/ "-O2" "-O2
  -fcrug-modern"` compares object bytes and finds zero differences. A flag is
  allowed to change what is accepted, never what is emitted.
- **Unknown options are rejected.** `-fcrug-nonexistent` is an unrecognized
  command-line option, not a silently ignored one. A silently ignored flag is
  how a build system ends up believing it tested the feature.
- **Macros may use the words.** `gcc/testsuite/crug.dg/flags/macro-regression.c`
  defines `own(x)`, `move(x)`, `go(label)` and `chan` as macros and compiles
  with `-fcrug-modern` and with `-fcrug-kernel`; the intrinsic and qualifier
  readings both require an undeclared identifier in a specific position, so a
  project's macros keep working.

## 5. What is not implemented yet

- `own`, `move`, `adopt`, `release`, `drop` and automatic destruction exist
  (`OWNERSHIP.md`, `DROP.md`); ownership transfer at call sites does not.
- The lexical borrow rules of Phase 6 exist (`BORROW.md`): whole-object
  borrows, the conflict matrix, lexical lifetime, `ref`/`mut` parameters as
  live borrows. No call-site checking (Phase 8), no member or array-element
  borrows, no mutable reborrow, no NLL (Phase 7), no struct fields.
- No goroutine, scope or channel lowering (Phase 14-16).
- Only the `ownership`, `drop` and `borrow` dumps exist (`-fcrug-dump=` comma
  list); no goroutine or channel dump.
- Six `-Wcrug-*` options are declared and default on
  (`-Wcrug-unadopted-own`, `-Wcrug-intrinsic-operand`, `-Wcrug-adopt-signature`,
  `-Wcrug-jump-into-scope`, `-Wcrug-setjmp-drops`, `-Wcrug-drop-unreachable`);
  `-fcrug-strict` promotes them to errors. The always-error ownership
  diagnostics carry no option.
- No change to the driver beyond option acceptance: `gcc/crugc` is still the
  Phase 1 launcher.

## 6. Tests

| Check | What it proves |
| --- | --- |
| `make check-CRUG_FLAGS` / `scripts/check-crug-flags.sh` | every flag alone, the macro closure of each, every accepted pair, the three kernel conflicts, the unknown-option rejection, and the words fixture |
| `scripts/diff-obj.sh corpus/ "-O2" "-O2 -fcrug-modern"` | object bytes unchanged by the full flag set |
| `make check-GNU_C_COMPAT` / `scripts/check-gnu-c-compat.sh` | the 30-program GNU C corpus and the 18-file kernel corpus still compile with the flags on |
| `gcc/testsuite/crug.dg/flags/` | fixtures for the future DejaGnu tests named in ADR 0018 D1 |
| `make check-OWNERSHIP` | `own` and `move` productions with the flag on: 17 expected passes under `gcc/testsuite/crug.dg/ownership/` |
| `crugc -fcrug-dump=ownership` | the ownership dump of a branch join (`OWNERSHIP.md` section 4) |
| `make check-DROP` | destruction and its rejections: 39 expected passes under `gcc/testsuite/crug.dg/drop/` |
| `scripts/check-drop-dump.sh` | the `drop` dump records every exit kind (`DROP.md` section 6) |
| `make check-BORROW` | `ref`, `mut`, `borrow` and `borrow_mut` with the flag on: 62 expected passes under `gcc/testsuite/crug.dg/borrow/` |
| `scripts/check-borrow-cost.sh` / `check-borrow-abi.sh` | a borrowed function compiles to the same object as its plain-pointer twin, and `ref`/`mut` do not change `sizeof` or `_Alignof` |
| `scripts/check-borrow-dump.sh` | the `borrow` dump records shared and mutable borrows and their ends (`BORROW.md` section 4) |

The phase-3 gate runs the first four in order, the phase-4 gate runs the
ownership rows plus the ABI and diagnostic-code checks, the phase-5 gate runs the
six `scripts/check-drop-*.sh` and `scripts/check-diagnostics.sh` checks, and the
phase-6 gate runs the five `scripts/check-borrow-*.sh` scripts, one compile-fail
exit status and `scripts/check-diagnostics.sh`; see `EXIT-GATES.md`.

## 7. Adding a flag

A new flag needs: a new bit in `gcc/c-family/crug-common.h`, a `Common
Var(...)` entry in `gcc/common.opt`, a row in the closure table of
`crug_apply_option_closure`, a feature macro line in `c-cppbuiltin.cc`, a
closure case in `scripts/check-crug-flags.sh`, and an ADR 0018 amendment if it
interacts with an existing implication. The Phase 3 gate has to keep passing
unchanged.
