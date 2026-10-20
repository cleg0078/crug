# Crug compiler internals

Status: **Phases 0-22 implemented and measured, 2026-10-20.** The fork delta and every attachment point on this page were read from the tree with `scripts/fork-delta.sh`; timings are from the reference host (Linux x86-64, 20 logical CPUs).

## 1. Repository layout

The repository root is the GCC top level. There is no separate fork directory: the pinned `releases/gcc-16.2` tree is committed in place, and Crug adds files and textual hooks to it. The pinned tag is documented in `docs/crug/GCC-BASELINE.md` and `docs/crug/gcc-baseline.sha512`.

| Path | Contents |
|------|----------|
| `gcc/c/` | The C front end: `c-parser.cc`, `c-decl.cc`, `c-tree.h`, plus the Crug modules `crug-ownership.*`, `crug-safe.*`, `crug-tasks.*`, `crug-chan.*`, `crug-send-sync.*`. |
| `gcc/c-family/` | Shared front-end code: `c-opts.cc`, `c-cppbuiltin.cc`, `c.opt`, plus `crug-common.*`, `crug-ffi.*`, `crug-diagnostics.*`. |
| `gcc/` | `common.opt` (the `-fcrug-*` options), `Makefile.in` and `c/Make-lang.in` (object lists), `diagnostics/sarif-sink.cc`, and the `crugc` driver wrapper. |
| `gcc/testsuite/crug.dg/` | Ten `.exp` drivers and 321 DejaGnu fixtures for ownership, drop, borrow, strict, ffi, safe-types, alias, goroutines, scope, channels, send-sync, diagnostics, flags, kernel, optimization. |
| `libcrug/` | The runtime: `core/` (tasks, channels, PAL contract), `pal/` (`linux`, `darwin`, `windows`, `fake`, `include`), `arch/` (x86_64, aarch64), `tests/`. |
| `kernel/` | Kernel probes, the pinned object corpus list and `object-diff-allow.txt`; `scripts/kernel-stage.sh` drives K1-K7. |
| `fuzz/` | `compiler/` (parser, ownership, borrow, ffi targets, seeds) and `runtime/` (scheduler and channel harnesses). |
| `migration/`, `corpus/`, `examples/`, `bench/` | The c2crug corpus, the 30-file codegen corpus, runnable examples, and the benchmark/verification tool. |
| `scripts/`, `docs/crug/`, `spec/` | Checkers and gate tooling; documentation and ADRs under `docs/crug/adr/`; RFC text under `spec/`. |
| `build/`, `tools/`, `out/` | Build output, the DejaGnu bootstrap and scratch trees; all ignored, never committed. |

The driver `gcc/crugc` is a 39-line POSIX shell wrapper. It resolves a real driver in the order `CRUGC_REAL_DRIVER`, `build/obj/gcc/xgcc`, `gcc/xgcc`, `gcc/gcc`, executes it with `-B<dir>`, and otherwise prints `crugc: no GCC driver found` and exits 1. It adds no behavior of its own: behavior lives in the compiler modules, so a wrapper change is not a language change.

## 2. The fork delta

`scripts/fork-delta.sh` separates the two kinds of delta: upstream files the fork edits, which are replayed on a new baseline, and files only Crug has, which move with the tree.

Measured on 2026-10-20 (`HEAD 7b7f8dbb94da`, 155 commits since tag `releases/gcc-16.2`): **10 upstream files, 63 hunks, +2812/-41 lines; 17 new sources, 8,989 lines; 405 new testsuite files** (not listed by the script).

| Upstream file | Hunks | +ins | -del | Why it is touched |
|---------------|------:|-----:|-----:|-------------------|
| `gcc/c/c-parser.cc` | 36 | 2,437 | 31 | Qualifier lookahead, specifier parsing, statement forms, intrinsics, `.crugffi` finish. |
| `gcc/c/c-decl.cc` | 12 | 131 | 2 | Mark declarations and parameters, member and function rules. |
| `gcc/c/c-tree.h` | 2 | 14 | 0 | `enum crug_qualifier_kind` and the `crug_qual` bitfield in `struct c_declspecs`. |
| `gcc/c-family/c-cppbuiltin.cc` | 2 | 35 | 0 | Define the feature macros in Crug mode. |
| `gcc/c-family/c-opts.cc` | 3 | 21 | 0 | FFI option dispatch, option closure, FFI setup. |
| `gcc/c-family/c.opt` | 1 | 49 | 0 | Warning options, `-fcrug-ffi=`, `-fcrug-bounds-checks`. |
| `gcc/common.opt` | 1 | 60 | 0 | The `-fcrug-*` options, including `-fcrug-dump=`. |
| `gcc/diagnostics/sarif-sink.cc` | 4 | 61 | 6 | Carry the stable code into SARIF as `ruleId`. |
| `gcc/Makefile.in`, `gcc/c/Make-lang.in` | 2 | 4 | 2 | Object lists for the Crug modules. |

New sources: `crug-ownership.cc` 4,620 and `.h` 143 (moves, drop placement, borrow checker, intrinsics, dumps); `crug-safe.cc` 900 and `.h` 158 (slices, option/result, ADT payload enums, bounds checks, match helpers); `crug-ffi.cc` 1,289 and `.h` 72 (sidecar parse and contract); `crug-common.cc` 117 and `.h` 63 (feature bits, option closure); `crug-diagnostics.cc` 73 and `.h` 38 (coded diagnostics); `crug-tasks.cc` 529 and `.h` 70 (`go`, `yield`, `scope`); `crug-chan.cc` 469 and `.h` 64 (channel types and select); `crug-send-sync.cc` 295 and `.h` 50 (Send/Sync, thread affinity); `gcc/crugc` 39.

The totals are worth reading correctly. The 63 hunks are the rebase surface; the 8,989 lines are not, because a new upstream tree never touches files it does not have. Section 6 records what a version bump actually costs.

Note a documentation inconsistency: `docs/crug/GCC-UPGRADE.md` section 2 still records the 2026-10-19 measurement of 9 files, 32 hunks, +495/-6 and 5,021 lines. That figure predates phases 13-22, which added the tasks, channel, send-sync, safe-types and diagnostics code. The current tree is the 10/63/+2812/-41 figure above; treat the older one as historical, not as a target.

## 3. Attachment points

Every hook is a textual insertion into a front-end function. That is deliberate: a GCC plugin cannot add declaration specifiers or own the parser, so the integration has to live in the parser itself. The anchors below are the ones a rebase must keep working.

Stability is graded, and the grade is the rebase plan. The stable rows (option tables, build plumbing, predefined macros) need no work on a version bump. The medium rows are the parser and declaration hooks, where a conflict may appear but the meaning is obvious. Two rows are fragile: the `struct c_declspecs` layout and bitfield style, which has already drifted once, and the `c_parse_file` sidecar finish, which relies on file-scope lookup still seeing the declarations at that point. The fragility, not the line count, is what makes a major upgrade unpredictable.

`gcc/c/c-parser.cc`
- Headers: the Crug includes sit with the other front-end includes after `config.h`.
- Qualifier lookahead: `crug_qualifier_token_kind_n`, `crug_qualifier_token_kind`, `crug_qualifier_token_p` and `crug_qualifier_type_name_p` classify `own`, `raw`, `ref` and `mut` as qualifier names rather than identifiers.
- `c_parser_declspecs`: parses a generic type at the generic-type branch and a channel type at the channel branch, and stores the qualifier in `struct c_declspecs`.
- `c_parser_declaration_or_fndef`: after the body is parsed, calls `crug_check_function (fndecl, fnbody, crug_function_has_scope (fndecl))` when ownership, borrow or scope is active, then `crug_forget_affine_decls`.
- `c_parser_statement_after_labels`: dispatches `match`, `unsafe` blocks, and the `go`, `scope`, `select` and `yield` statement forms.
- `c_parser_cast_expression`: applies the strict cast policy when `crug_strict_enabled ()` and the unsafe depth is zero.
- Later in the file, the modules define `crug_parse_generic_type`, `crug_parse_constructor`, `crug_parse_go_statement`, `crug_parse_yield_statement`, `crug_parse_scope_statement`, `crug_parse_channel_type`, `crug_parse_select_statement` and `crug_parse_match`.

`gcc/c/c-decl.cc`
- `start_decl`: marks an owning, raw or borrowed declaration from `declspecs`.
- `push_parm_decl`: marks `own`/`raw`/`borrow` parameters and calls `crug_apply_alias_fact` for alias facts.
- `grokfield`: rejects a borrowed member (`crug-BORROW-007`), raw or own on a bit-field, flexible array or unnamed member (`crug-TYPE-006`), and an owning member in a union (`crug-OWN-009`).
- `start_function`: rejects a function returning a borrow (`crug-BORROW-001`).

`gcc/c/c-tree.h` holds `enum crug_qualifier_kind` and the three-bit `crug_qual` field. `gcc/c-family/c-opts.cc` intercepts `-fcrug-ffi=` in `c_common_handle_option` and calls `crug_apply_option_closure ()` and `crug_ffi_setup ()` from `c_common_post_options`. `gcc/c-family/c-cppbuiltin.cc` defines the feature macros in `c_cpp_builtins` only when `crug_features` is nonzero. `gcc/c-family/c.opt` and `gcc/common.opt` declare the options; `gcc/c/Make-lang.in` and `gcc/Makefile.in` list the objects. `gcc/diagnostics/sarif-sink.cc` builds a Crug code into the `ruleId` in `make_rule_id_for_diagnostic_kind`.

The feature bits live in `gcc/c-family/crug-common.h`: OWNERSHIP, BORROW, SLICES, RESULT, ADT, GOROUTINES, CHANNELS, STRICT, KERNEL and ALIAS_FACTS. `crug_apply_option_closure ()` expands `-fcrug-modern` to the modern set (which excludes kernel deliberately), then adds the dependencies: borrow and alias facts need ownership, alias facts need borrow, channels need goroutines, result needs ADT, strict needs ownership and borrow. Kernel with goroutines, channels, ownership or borrow is a hard error, because the kernel profile must not link the runtime.

## 4. Passes and lowering

There is no Crug GIMPLE pass. Everything happens in the C front end as a GENERIC-to-GENERIC transform between parsing and gimplification, which is why the dumps are front-end dumps and why an upstream middle-end change rarely touches Crug.

The entry point is `crug_check_function` in `gcc/c/crug-ownership.cc` (starts around line 4489). It returns immediately for a null or error body, or when ownership, borrow and scope are all off. Otherwise it rejects owning payload-enum parameters and returns (`crug-ADT-018`), builds a function context, collects labels, seeds the environment with owning and borrowed parameters, dumps alias facts, computes borrow liveness (last-use based), walks the body, and on reach collects drops into the scope's drop list and dumps them. Goto checks run before rewrite. Then a `walk_tree` pass inserts destruction on every exit edge and removes the remaining intrinsics. The registration maps for match, scope and go are cleared at the end.

The pieces it uses are ordinary front-end APIs: `BIND_EXPR`, statement lists, `LABEL_DECL`, `add_stmt`, `build_call_expr_loc`, `mark_exp_read` and `DECL_UID`. Nothing depends on a GCC pass manager, which is why `-fcrug-dump=ownership,drop,borrow,alias` works before gimplification and why the dumps are stable across `-O` levels.

Other lowering:
- `go`, `scope` and `yield` become hidden runtime calls through `gcc/c/crug-tasks.cc`: `crug_go_lower`, `crug_scope_enter_call`, `crug_scope_leave_call`, `crug_yield_call`, with `crug_note_scope_function` and `crug_function_has_scope` deciding whether the analysis runs. Generated trees call `crug_task_spawn`, `crug_task_spawn_scoped`, `crug_task_thunk_<n>` and `crug_task_yield`.
- Channel operations through `gcc/c/crug-chan.cc` create calls to `crug_chan_make`, `send`, `recv_ptr`, `recv_owned`, `recv_ready`, `send_ready`, `select_turn`, `poll_wait` and `close`; the owned send/receive checks are `crug_channel_send_owned_call_p` and `crug_channel_recv_owned_call_p`.
- `-fcrug-alias-facts` does not generate code: `crug_apply_alias_fact` adds a `restrict` qualifier to a mutable or owning pointer parameter, and the dump records it. That is the whole alias model, and it is why the benchmark can demand byte identity with the restrict arm.
- `gcc/c-family/crug-ffi.cc` parses the `.crugffi` sidecar, matches declarations by name, and answers `returns_owned`, `arg_consumes`, `arg_out_owned`, `arg_callback`, `arg_borrow`, `arg_nullable`, `thread_affine` and type flags. It never generates stubs.
- `gcc/c/crug-safe.cc` lowers slices, option/result, payload enums, bounds checks and match to trees and calls; `gcc/c/crug-send-sync.cc` answers Send/Sync, thread affinity and `crug_forget_affine_decls`.

The pass is single-tree-walk per function: one reachability walk collects drops and liveness, one rewrite walk inserts the destruction calls, and one lowering walk removes the intrinsics. No state survives a function, so compilation order does not change a diagnostic or a dump. That property is what the determinism checks in the fuzz and goldens suites rely on, and it is why a dump diff is a meaningful review artifact.

## 5. Adding a feature

A flag is a feature bit plus an option row plus a closure rule. Concretely: add a bit to `gcc/c-family/crug-common.h`, a `Common Var(...)` row to `gcc/common.opt` (or `gcc/c-family/c.opt` for a warning), a row in the closure table of `crug_apply_option_closure` in `gcc/c-family/crug-common.cc`, a feature macro line in `gcc/c-family/c-cppbuiltin.cc`, a closure case in `scripts/check-crug-flags.sh`, and an ADR 0018 amendment if the flag interacts with an implication. Re-run `make check-CRUG_FLAGS` and the phase-3 gate; a flag that is accepted but not closed into `crug_features` is a silent no-op, and the flag suite is what catches it.

A syntax feature needs a parser hook in `gcc/c/c-parser.cc` (or `c-decl.cc` for declarations), the marking function in the right module, and analysis in `crug_check_function` or the module's own walk. Its acceptance and rejection cases go in `gcc/testsuite/crug.dg/<dir>`; the feature bit gates them so that plain C is untouched.

A diagnostic is four steps from `docs/crug/DIAGNOSTICS.md` section 7: record the origin decision; add the registry row with default and strict severity; add `gcc/testsuite/crug.dg/diagnostics/<code>.c` whose `dg-error` names the code; add the option row when it is a warning. At the call site use `crug_error_at` or `crug_warning_at` with the code (both build a precanned rule with the documentation URL) and `crug_note_at` for notes, which carry no code. Then run `scripts/check-diagnostic-goldens.sh --update`, review the new `golden/<code>.txt`, and re-run `scripts/check-diagnostics.sh`, which fails on an unregistered code, an unknown area or a duplicate.

A new feature also needs its observable: a dg fixture that fails without the feature, a script check when the observable is a symbol, count, object or dump line, and a gate CHECK only when the phase owner wants it gated. Without that, a feature is present in the code and absent from the evidence, which is the one state the exit gates treat as open.

A new upstream-facing behavior should come with a test at the layer that observes it and with a line in `docs/crug/EXIT-GATES.md` only when the phase owner wants it gated. Add scripts under `scripts/check-*.sh` following the house conventions (root via `dirname`, one tally line, exit 2 on setup error, scratch under `build/`) and register a make target only for a gated suite.

## 6. Upgrading GCC

The fork is a small patch on a pinned release, so an upgrade is a rebase plus verification, not a rewrite. `scripts/fork-delta.sh` prints the rebase surface (the 10 upstream files above); `scripts/check-gcc-drift.sh` reports how a newer upstream tree differs, checking hunk contexts, the attachment symbols and eight behavioural pins, and exits 0 only when every remaining drift is recorded in `docs/crug/gcc-drift-known.txt` with a reason (1 unknown drift, 2 network or repository unreadable).

The procedure is seven steps, each a commit on `gcc-upgrade/X.Y.Z`:
1. Classify the change (patch, minor or major) per ADR 0022 and branch.
2. Pin the release: SHA-512 into `docs/crug/gcc-baseline.sha512`, version and URL into the `Makefile`, the pin table into `docs/crug/GCC-BASELINE.md`, plus the ADR set.
3. Import the pristine tarball over the root, keep the Crug-only paths, merge `.gitignore`, stage with an explicit manifest and tag `releases/gcc-X.Y.Z` (annotated, never retagged).
4. Replay the fork with `git rebase --onto <import> releases/gcc-16.2 main`. Only the 10 files can conflict; resolve with the section 3 map, then confirm with `scripts/fork-delta.sh --tag releases/gcc-X.Y.Z` that the hunk and line counts are unchanged apart from new work.
5. Rebuild from the in-repository tree: `rm -rf build/obj` then `make baseline`, and `make build-full` if tests need target libraries.
6. Re-baseline and verify: `make baseline-phase0` re-runs `gcc.dg` (about two hours), every new FAIL must be fixed or allowlisted with a reason, then `scripts/verify-upgrade.sh` runs the 31-check matrix and `--gates` adds phases 0-9.
7. Record and push: update `docs/crug/progress.md`, the baseline pin and the ADRs, commit the evidence, and let `kernel-compat` run on the new baseline. The upgrade is not done at a local commit.

The pins are the part a rebase must not rediscover: the `libcpp` include callback passes the directive name (P1); `c_build_bind_expr` returns the bare body when nothing is declared (P2); `BIND_EXPR_VARS` is a `DECL_CHAIN` (P3); file-scope `lookup_name` still sees declarations at the end of `c_parse_file` (P4); a `Common` option without `Var` asserts (P5); `aux_base_name` names dumps and `mark_exp_read` feeds `-Wunused-but-set-variable` (P6); GCC needs an ISO C++14 host compiler (P7); GCC 16 has no `EXPR_STMT` or `STMT_EXPR` tree codes (P8). P1 and P5 were found the hard way and are exactly what a version bump would otherwise repeat.

The recorded risks are the same four that make any major rebase expensive: a refactor of the declaration parser, a change to the `struct c_declspecs` layout, a change to the `libcpp` include callback contract that sidecar discovery depends on, and a move of the host compiler requirement. None of them touches the Crug module bodies, which is why the module count does not grow the upgrade estimate. The risk register in `docs/crug/GCC-UPGRADE.md` section 7 records likelihood, impact and the mitigation for each.

The current baseline is GCC 16.2.0 (released 2026-08-07); 16.3 and 17.0 are unreleased as of 2026-10-20, so the next realistic move is a patch release or 17.0 once it ships. Measured effort is about one day for a patch release and 2-5 working days typical for a major, dominated by the two-hour `gcc.dg` re-baseline and the gates, not by porting: the 8,989 lines of Crug code use tree and parser APIs GCC keeps stable for years. Nothing in the module tree changes for a version bump; only the hooks and the eight pins move.

The module tree is version-independent for the same reason: it calls the front-end APIs rather than the internals of a pass.
