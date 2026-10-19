# Crug feature flags and feature macros

Status: **Phase 3 implemented; Phases 4 to 14 use it** (2026-10-19). The option
set, its closure and the feature macros exist.
`-fcrug-ownership` gates the `own`, `move` and drop productions of Phases 4
and 5 (`OWNERSHIP.md`, `DROP.md`), `-fcrug-borrow` gates `ref`, `mut`,
`borrow` and `borrow_mut` (`BORROW.md`), `-fcrug-strict` adds the
unchecked-operation policy with `raw` and `unsafe` (`STRICT.md`),
`-fcrug-ffi=<file>` reads a contract sidecar (`FFI.md`), and
`-fcrug-slices`/`-fcrug-result` gate the safe core types, the value
constructors and `match` (`SAFE-TYPES.md`), and `-fcrug-alias-facts`
turns a proven `mut` or `own` parameter into a `restrict` fact
(`OPTIMIZATION.md`), and `-fcrug-goroutines` gates the `go`, `yield`
and `scope` statements (`GOROUTINES.md`, `SCOPE.md`); the
productions the remaining flags name (`chan`, `select`, the kernel port)
do not exist yet. This page is the digest - the normative text is
ADR 0018 D1 and D9, ADR 0021, RFC-0001 sections 10, 13 and 20, and the gates are
`EXIT-GATES.md` phases 3, 4, 5, 6, 7, 8, 9 and 10.

## 1. The option set

The feature options are declared in `gcc/common.opt`, all `Common` so that
the driver and every front end accept them, and all with a `Var(...)` so the
closure is computed in one place (`gcc/c-family/crug-common.cc`).

| Option | Enables, when its phase lands | Implies |
| --- | --- | --- |
| `-fcrug-ownership` | `own`, `raw`; `move`, `drop`, `adopt`, `release`; `unsafe {}` | - |
| `-fcrug-borrow` | `ref`, `mut`; `borrow`, `borrow_mut` | `-fcrug-ownership` |
| `-fcrug-alias-facts` | compiles `mut` and `own` pointer parameters as `restrict` and rejects unproven call sites (`OPTIMIZATION.md`) | `-fcrug-borrow` |
| `-fcrug-slices` | `slice<T>`, `slice (ptr, len)`, checked `s[i]` | - |
| `-fcrug-result` | `option<T>`, `result<T,E>`, `some`, `none`, `ok`, `err`, `match` | `-fcrug-adt` |
| `-fcrug-adt` | payload enums and `match` over `option`, `result` and payload enums | - |
| `-fcrug-goroutines` | `go` and `yield` (`GOROUTINES.md`) and `scope` (`SCOPE.md`) | - |
| `-fcrug-channels` | `chan T`; `send`, `recv`, `close`, `channel`; `select` | `-fcrug-goroutines` |
| `-fcrug-strict` | no production of its own: it is a checking mode. Promotes every `-Wcrug-*` warning to an error and enforces the unchecked-operation policy (`STRICT.md`) | `-fcrug-ownership`, `-fcrug-borrow` |
| `-fcrug-ffi=<file>` | reads one `.crugffi` sidecar; repeatable, merged in command-line order (`FFI.md`) | `-fcrug-ownership` |
| `-fcrug-ffi-discover` | turns the FFI layer on without naming a file, so sidecars next to quoted includes (and in `CRUG_FFI_PATH`) are discovered | `-fcrug-ownership` |
| `-fcrug-modern` | everything above | all eight flags |
| `-fcrug-kernel` | no production; kernel mode | excludes `-fcrug-goroutines`, `-fcrug-channels`, `-fcrug-modern`; rejects `-fcrug-ownership` and `-fcrug-borrow` until Phase 19 |

`Three more options live in `gcc/c-family/c.opt`, because they are C-family
decisions rather than driver-wide switches, and are handled in
`c_common_handle_option`: `-fcrug-ffi=<file>`, `-fcrug-ffi-discover`
(`FFI.md`) and `-fcrug-bounds-checks` / `-fno-crug-bounds-checks`
(`C Var(flag_crug_bounds_checks) Init(1)`), which selects whether `s[i]` on a
`slice<T>` emits the length comparison and the `__builtin_trap` of
`SAFE-TYPES.md` section 8. It changes no accepted syntax and no other lowering.

`-fcrug-dump=<kind>` is a debugging option, not a feature flag: it is
`Common Joined RejectNegative` with a comma-separated list of kinds -
`ownership`, `drop`, `borrow` and `alias` today; an unknown kind is an
error. It writes `<stem>.crug-ownership`, `<stem>.crug-drop`,
`<stem>.crug-borrow` or `<stem>.crug-alias` next to the output base while the
front end runs, and changes no code. `OWNERSHIP.md` section 4, `DROP.md`
section 6, `BORROW.md` section 4 and `OPTIMIZATION.md` section 4 give the
formats.

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
| `__CRUG_ALIAS_FACTS__` | `-fcrug-alias-facts` after closure |
| `__CRUG_SLICES__` | `-fcrug-slices` |
| `__CRUG_RESULT__` | `-fcrug-result` |
| `__CRUG_ADT__` | `-fcrug-adt` after closure |
| `__CRUG_GOROUTINES__` | `-fcrug-goroutines` |
| `__CRUG_CHANNELS__` | `-fcrug-channels` |
| `__CRUG_STRICT__` | `-fcrug-strict` |
| `__CRUG_FFI_VERSION__` | `100` when `-fcrug-ffi=` or `-fcrug-ffi-discover` is given (v1.0) |
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
  (`OWNERSHIP.md`, `DROP.md`). Ownership transfer at a call site exists
  for a callee with a `.crugffi` contract (`FFI.md`); an unannotated callee still
  cannot take ownership of an argument.
- The borrow rules of Phases 6 and 7 exist (`BORROW.md`): whole-object
  borrows, the conflict matrix, last-use lifetimes, `ref`/`mut` parameters as
  live borrows, and the depth-zero interprocedural policy (ADR 0019). No member
  or array-element borrows, no mutable reborrow (a later phase), no struct
  fields.
- Strict mode, `raw` and `unsafe` exist (`STRICT.md`); there is no
  provenance or UB sanitizer, and `unsafe` code is not analysed.
- Goroutine lowering exists (Phase 14): `go f (args);`, `go (call);` and
  `yield ()` lower to a per-call-site frame struct, a static thunk and one
  `crug_task_spawn` call (`GOROUTINES.md`); the capture rules are CONC-008,
  CONC-010 to CONC-014, and a capture needs `-fcrug-ownership` for an `own`
  value or `-fcrug-borrow` for a borrow. Structured scope exists (Phase 15):
  `scope { ... }` is a task group that waits for its children, a scoped child
  may capture a borrow of a value that outlives the block, and the extra codes
  are CONC-015 (a goto into a scope) and CONC-016 (a lender that dies first)
  (`SCOPE.md`, ADR 0024). There is no channel, `send`, `recv`, `close`,
  `channel` or `select` lowering (Phase 16).
- Ownership-aware optimization exists as an opt-in contract
  (`-fcrug-alias-facts`, `OPTIMIZATION.md`, ADR 0010): `mut` and `own`
  pointer parameters become `restrict` and the caller must pass a proven
  mutable borrow. It is off by default and `-fcrug-modern` does not enable it,
  because the fact changes code generation and makes an unproven call site
  undefined. Without the flag, a plain pointer at a `mut` parameter is still
  accepted (`BORROW.md` section 2.4).
- Only the `ownership`, `drop`, `borrow` and `alias` dumps exist
  (`-fcrug-dump=` comma list); no goroutine or channel dump.
- `slice<T>`, `option<T>`, `result<T,E>`, their value constructors, `match`
  over all three sum shapes, payload enums and checked slice indexing exist
  (`SAFE-TYPES.md`). Payload enums are gated by `-fcrug-adt` exactly as
  described there: an enum with no parenthesised payload stays an ordinary C
  enum, so the flag still cannot change existing C. An owned payload
  (`own` inside the payload parentheses) additionally needs
  `-fcrug-ownership`; the payload is then transferred by `adopt`, `move` or a
  fresh allocation, dropped only when its variant is active, and moved into
  `match` (`SAFE-TYPES.md` section 6). `option` and `result` payloads are not
  tracked yet.
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
| `make check-BORROW` | `ref`, `mut`, `borrow` and `borrow_mut` with the flag on, including last-use lifetimes and the call policy: 88 expected passes under `gcc/testsuite/crug.dg/borrow/` |
| `make check-STRICT_UNSAFE` | strict mode, `raw` and `unsafe`: 21 expected passes under `gcc/testsuite/crug.dg/strict/` |
| `make check-FFI` | `.crugffi` contract fixtures: 19 cases, 0 failures, under `gcc/testsuite/crug.dg/ffi/phase9/` |
| `make check-SAFE_TYPES` / `scripts/check-safe-types.sh` | `slice`, `option`, `result`, `match`, payload enums and the frozen layouts: 77 expected passes under `gcc/testsuite/crug.dg/safe-types/` |
| `scripts/check-owned-payloads.sh` | an owning payload enum drops the active variant once and an inactive one never, the generated code guards with the tag, and the six unsafe forms are rejected (`SAFE-TYPES.md` section 6) |
| `scripts/check-safe-bounds.sh` | in-range indexing runs, an out-of-range index traps with `SIGILL`, and `-fno-crug-bounds-checks` removes the trap |
| `scripts/check-ffi-*.sh` | the metadata reads the same twice (conflict determinism), four real libraries keep their hashes while their handles are closed exactly once, and objects are byte-identical with and without a sidecar |
| `scripts/check-strict-*.sh` | the codegen identity (objects byte-identical with and without `-fcrug-strict`), the hybrid-vs-strict answer for one raw dereference, and the eight strict codes observed |
| `scripts/check-borrow-cost.sh` / `check-borrow-abi.sh` | a borrowed function compiles to the same object as its plain-pointer twin, and `ref`/`mut` do not change `sizeof` or `_Alignof` |
| `scripts/check-borrow-dump.sh` | the `borrow` dump records shared and mutable borrows and their ends (`BORROW.md` section 4) |
| `scripts/check-borrow-nll.sh` / `check-borrow-escape.sh` / `check-borrow-real-c.sh` | a borrow ends at last use in the dump, an escaping borrow is rejected with its origin noted, and six realistic programs compile and run |
| `make check-ALIAS_FACTS` | `mut`/`own` alias facts, the flag-off leniency, and the five call-site rejections (BORROW-002, -003, -021, -022, -023): 16 expected passes under `gcc/testsuite/crug.dg/alias/` |
| `scripts/check-optimization.sh` | the golden alias dump, 8/8 benchmark kernel objects byte-identical to their hand-written `restrict` twins, ref-only and flag-off identity, five adversarial rejections and one runtime program |
| `scripts/check-stop-conditions.sh` | every dumped fact names a `mut`/`own` parameter, a ref-only function has no fact, and 0 false-noalias reports |
| `bench/run.py --matrix alias --check` / `--lto` / `--plain-c --check` | the 5% band with byte identity as the primary evidence, LTO correctness, and plain C not degraded (object identity, 1.02 band when code changes) |
| `make check-GOROUTINES` / `scripts/check-goroutines.sh` | `go`/`yield` lowering: 27 DejaGnu cases, five runtime programs, 100000 tasks on 4 workers and 4 thread-creating calls under `strace` (`GOROUTINES.md`, `TASKS.md`) |
| `make check-SCOPE` / `scripts/check-scope.sh` | `scope` lowering: 23 DejaGnu cases and eight group programs whose counters balance (`SCOPE.md`, ADR 0024) |

The phase-3 gate runs the first four in order, the phase-4 gate runs the
ownership rows plus the ABI and diagnostic-code checks, the phase-5 gate runs the
six `scripts/check-drop-*.sh` and `scripts/check-diagnostics.sh` checks, and the
phase-6 gate runs the five `scripts/check-borrow-*.sh` scripts, one compile-fail
exit status and `scripts/check-diagnostics.sh`; the phase-7 gate runs the eight
borrow scripts, the two compatibility suites and `scripts/check-diagnostics.sh`;
the phase-8 gate runs the four `scripts/check-strict-*.sh` scripts, the two
compatibility suites and `scripts/check-diagnostics.sh`; the phase-9 gate runs
`make check-FFI` and the four `scripts/check-ffi-*.sh` scripts plus
`scripts/check-diagnostics.sh`; the phase-10 gate runs `make check-SAFE_TYPES`,
`scripts/check-safe-types.sh`, `scripts/check-safe-bounds.sh`,
`scripts/check-owned-payloads.sh`, the layout compiles and the two
compatibility suites; the phase-11 gate runs `bench/run.py --matrix alias
--check`, `scripts/check-optimization.sh`, `make check-OPTIMIZATION`,
`bench/run.py --plain-c --check`, `scripts/check-stop-conditions.sh`,
`scripts/check-diagnostics.sh` and `make check-C_COMPAT`; the phase-14 gate
runs `make check-GOROUTINES`, the 100000-task stress run, the `strace` clone
count, `make -C libcrug check-sanitizer`, the kernel object check,
`scripts/lint-no-os-apis.sh`, `scripts/check-diagnostics.sh` and
`make check-C_COMPAT`; the phase-15 gate runs `make check-SCOPE`, the
scope-borrow and goto rejections, `make -C libcrug check-sanitizer`,
`scripts/lint-no-os-apis.sh`, `scripts/check-diagnostics.sh` and
`make check-C_COMPAT`; see `EXIT-GATES.md`.

## 7. Adding a flag

A new flag needs: a new bit in `gcc/c-family/crug-common.h`, a `Common
Var(...)` entry in `gcc/common.opt`, a row in the closure table of
`crug_apply_option_closure`, a feature macro line in `c-cppbuiltin.cc`, a
closure case in `scripts/check-crug-flags.sh`, and an ADR 0018 amendment if it
interacts with an existing implication. The Phase 3 gate has to keep passing
unchanged.
