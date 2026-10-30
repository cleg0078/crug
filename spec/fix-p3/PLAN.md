# fix-p3: analysis, tasks and execution record

Source: the audit drop in `specs/fix-p3/` (`README.md`,
`OWNERSHIP_BORROW.md`, `TYPES_FFI_CONC.md`, `RUNTIME.md`, `TOOLING.md`
and the four `repro/` lanes). The drop reports 22 confirmed findings
plus three observations it marks SUSPECTED or OBSERVÉ (`OWN-05`,
`TYP-01`, `RT-03`), so the lanes carry 25 identifiers in all. Every
identifier was reproduced or re-derived on this tree before it was
adjudicated; the identifier, its fix, its regression and its ledger row
are recorded below.

Base commit `c43060459` (Crug 0.2.4). The batch is not released: it sits
on `fix-p3` under the `[Unreleased]` CHANGELOG heading.

## 1. How the batch was run

The four review lanes were split by root cause, not by report: one commit
per independent mechanism, and no commit touching another commit's
regression files. Two identifiers share a root cause and are fixed by
one commit (`OWN-02` and `OWN-02b` are the same missing do-while back
edge); one is documentation (`CONC-03`); one is a tooling regression the
fix-p2 batch introduced (`TL-01`). The last commit closes the tooling
group and the batch.

| Commit | Finding(s) | Area | Outcome |
|---|---|---|---|
| `6126fcb71` | OWN-01, OWN-02, OWN-02b | ownership | do-while body is walked as a probe, the back edge and every exit edge are joined, and the body is walked for real from the joined state |
| `25a0a2edc` | OWN-03 | borrow/alias | a lender value stored where the pass cannot follow is tracked as escaped and kills the exclusive fact |
| `31a4cc547` | OWN-04, OWN-05 | ownership | file-scope `own` is `crug-OWN-014`, an `own` array is `crug-TYPE-015` |
| `fb8ef616a` | OWN-05 follow-up | ownership | report the pointer-type rejection before the scope rejection |
| `33348caaf` | FFI-01, FFI-02 | ffi | EOF inside a sidecar declaration is `crug-FFI-013`; explicit sidecars parse lazily and their errors reach the SARIF sink |
| `90bcffb44` | CONC-01 | send-sync | depth 32 and beyond fails closed instead of returning true |
| `ce5d95546` | CONC-02 | send-sync | an assignment to an aggregate member records thread affinity |
| `dce29e744` | CONC-03 | channels/docs | `break` in a select arm is a parse error; leaving by `return`/loop is documented and pinned |
| `27b99d64e` | RT-01, RT-03 | tasks | the `in_flight` completion handshake is raised, and `start_state` is published before `started` |
| `a8507bda1` | RT-02, RT-04, RT-05, RT-06 | tasks/PAL | the wake test arms after runtime start; fake PAL excludes `recv_into`; Windows clock saturates; fake clock is atomic |
| `6222b9e46` | TL-01, TL-02, TL-03 | c2crug | control scanner restored, `--safe` publishes atomically, exit list and `#line` eligibility corrected |
| `89b2beb9f` | TL-04 (prune half) | packaging | `scripts/clean-dist.sh` and `make dist-prune` |
| `999c483c8` | TL-05 | packaging | SPDX symbolic-link entries and package verification code; the packaging check compares both sets |
| `e00af704a` | TL-04 (version half), TL-06 | tooling/packaging | c2crug reads `VERSION`; Debian Maintainer comes from data; build-examples quoting |
| `6401377bb` | TL-07 | fuzzing | the concurrency spellings entered the dictionary and two parser seeds |

## 2. Findings, fixes and evidence

### 2.1 OWN-01 / OWN-02 / OWN-02b: the do-while back edge

A do-while body was walked once from the loop-entry state and its exit was
recorded without joining the back edge. It was the only control-flow
construct in the ownership pass with that gap: `while`, `for`, `if`,
`switch` and backward `goto` already joined. A `move (p)` in the body
compiled with zero diagnostics and aborted with `free(): double free
detected`; `p = adopt (...)` leaked because the overwrite was invisible to
iteration two; `release (p)` followed by a read was a use-after-release.

The fix walks the body once as a side-effect-free probe (diagnostics, drop
records, exit records and scoped-lender consumption suppressed), joins the
probe's exit state with every recorded `break`/`continue`/`goto` edge
into the entry state, and then walks the body for real from that joined
state. The loop exit joins the entry state, the real body exit and the real
edges exactly as `while`/`for` do. Because the join is conservative,
`do { } while (0)` with a pre-loop owned value released in the body can
still report `crug-DROP-006`; that is the same answer the older loops give
and is recorded as a limitation rather than special-cased.

Regressions: `do-while-double-move.c`, `do-while-leak.c`,
`do-while-use-after-drop.c`, and the robustness cases
`do-while-fresh-alloc.c`, `do-while-continue.c`, `do-while-goto-out.c`.
Ledger K-043 and K-044.

### 2.2 OWN-03: an alias fact over an escaped view

The caller-side proof for a `mut` argument did not record a copy of the
lender stored into a file-scope object, a static, a structure member, an
array element or memory reached through a pointer. The injected
`restrict` then allowed -O2 to cache across an aliasing store: the audit
witness printed `a+b=0` where the defined answer is 2.

The pass now keeps an `escaped_values` set. A store into storage it cannot
follow records the roots of the stored value; `alias_roots` reports those
roots as unknown from then on; `check_borrow_call_args` rejects an
exclusive argument whose roots escaped with `crug-BORROW-024`. A fresh
allocation is exempt because it cannot be a view of an existing object.
An escape is never killed, which is deliberately conservative: the
alternative is a fact that is unsound at -O2. `docs/crug/BORROW.md`
section 2.4 and ADR 0010 D4 describe the rule.

Regressions: `alias/global-escape.c` and
`alias/global-store-field.c` (rejected), `alias/global-escape-fresh.c`
(accepted). Ledger K-045.

### 2.3 OWN-04 / OWN-05: declarations with no implementation

A file-scope `own` was accepted although the per-function ownership pass
never sees it: no drop was emitted, no use was diagnosed and
`-fcrug-strict` stayed silent. It is now `crug-OWN-014`. The check uses
the parser state rather than `DECL_FILE_SCOPE_P` because `DECL_CONTEXT` is
not set when the declaration is marked, the same reasoning
`crug_mark_borrow_decl` uses. A function-local static `own` remains
allowed and documented; the pass does not model its lifetime.

An `own T *arr[N]` was accepted on the strength of element-wise
destruction that never landed; every use was then `crug-UAM-008`, so the
surface was silently dead. The array declarator is now
`crug-TYPE-015`, for pointer and record element types. Commit
`fb8ef616a` only fixes the diagnostic ordering so a pointer element is
reported as a type error before the array scope error.

Regressions: `ownership/own-file-scope.c`,
`own-file-scope-static.c`, `own-array-of-pointers.c`,
`own-array-of-records.c`. `OWNERSHIP.md`, `GRAMMAR.md`, ADR 0018 and
`DIAGNOSTICS.md` are updated.

### 2.4 FFI-01 / FFI-02: a sidecar that ends inside a declaration

`take()` at EOF set the parser failure flag and returned an empty token,
and the two unguarded callers (a tag name after `type struct`/`type
opaque`, a function name after `fn`) let the empty name through. The file
was dropped without a diagnostic, so every contract it declared
disappeared and the caller was blamed instead.

`take()` now reports `crug-FFI-013` ("unexpected end of file inside a
declaration") once per file at the line of the last token. The guarded
sites keep the more specific `crug-FFI-003` only when no EOF diagnostic
was reported. `eof_reported` is initialised in `parse_ffi_file`; it was
previously read uninitialised, which made the new diagnostic depend on
stack contents.

Explicit sidecars now parse lazily, at the first contract query or at the
end of the translation unit, instead of during option processing, and
their errors go through `crug_error_at`. The text sink keeps the
`path:line:` prefix and appends `[crug-FFI-NNN]`; the SARIF sink gets a
`ruleId` and the `DIAGNOSTICS.md` helpUri for parse-time errors, which a
plain `error()` call dropped.

Regressions: `ffi/metadata/eof-type`, `eof-opaque`, `eof-partial`
(suite 30 to 33 cases), `scripts/check-ffi-policy.sh` and
`scripts/check-diagnostic-sarif.sh`.

### 2.5 CONC-01: inference past depth 32

`type_property_1` returned true once an aggregate or array nested more
than 32 levels, so a non-Send leaf beyond that point was never examined:
a 32-deep struct ending in `struct MTX *` was accepted as a scoped shared
borrow. It now returns false at the limit and sets `blocker` so the
diagnostic names the type that was not examined. `SEND-SYNC.md` section 8
documents the bound.

Regressions: `send-sync/chain-send-33.c` (`crug-CONC-024`),
`chain-sync-33.c` (`crug-CONC-026`), and the accepting boundaries
`chain-send-32.c` / `chain-deep-40.c`.

### 2.6 CONC-02 and CONC-03: affinity through a member, and the select-arm claim

A thread-affine value stored into an aggregate member was not tracked,
because the ownership pass only saw the call in initializer position:
`s.value = render_slot (); go use (s);` crossed a worker boundary with no
diagnostic. The affinity is now recorded at parse time (a `go` capture is
checked while parsing, before the ownership pass), and the member makes
the whole object affine. The property is conservative and one-way; a
plain copy or reassignment is still not tracked. Regressions:
`affine-field.c`, `affine-field-clean.c`, `affine-field-chan.c` with a
shared `affine-field.crugffi`; `SEND-SYNC.md` and ADR 0026 updated.

`CHANNELS.md` advertised `break` as the way an arm ends and claimed the
arm's destructions are collected. A top-level `select` is not a loop or a
switch, so a source-level `break` in an arm is an ordinary C parse error;
inside an enclosing loop it is accepted but leaves the whole `select`
after lowering. The break at the end of an arm in the lowering sketch is
emitted by the compiler to leave its own loop. `select-break.c` (two
`dg-error` lines) and `select-return.c` pin both rules; the channel suite
is 53 assertions over 32 fixtures.

### 2.7 TYP-01: an accepted slice length

`slice (p, -1)` is accepted and wraps the stored length; the bounds check
itself is sound (an out-of-range `s[i]` traps), so this is a silently
useless slice rather than a memory-safety hole. The audit marks it
SUSPECTED and by design for this batch: the constructor has no diagnostic
contract, and the accepted value cannot be used to read outside the
object because every indexed access re-checks the wrapped length. A
future `-Wcrug` warning could reject negative or oversized lengths at the
call site; that is recorded as residual work, not as a gate change.

### 2.8 RT-01 / RT-02 / RT-03: scope completion and start order

The fix-p2 `in_flight` handshake was dead code: `crug_task_finish`
dropped the group pending count and woke the group word while
`crug_scope_leave` could already observe pending zero and free the group.
It now raises `in_flight` before the decrement and drops it after the wake
returns, so `leave` waits for both counters. The ASan witness stops
reporting the freed-group wake. `scope_free_race.c` (3000 group lifetimes
through the main thread and the task-waiter path) joins
`check-RUNTIME` and `check-tasks-sanitizer.sh`.

The shipped `scope_wake_race.c` armed its PAL seam before the first
scoped spawn, so it parked the runtime-start wake instead of the group's
completion wake and could not see the use-after-free. It now starts the
runtime outside the seam, parks each group's completion wake, and
requires that `leave` cannot return while the wake is in flight; pre-fix
it reports `early_leave=1` and ASan reports the freed group, post-fix it
is clean. The seam test joins the ASan suite next to `scope_free_race`.

`runtime_start` published `started` before `start_state`, so a shutdown
could acquire `started == 1`, read the stale `start_state == 0` and join
worker handles the starter had not created yet. `start_state` is now
published first; the release store of `started` orders both writes. The
forced-pause repro runs CLEAN after the fix and still reports the old
ordering (status 3 with a NULL join) when the window is forced. The
natural window was never observed (0/600 attempts), so RT-03 has no
committed regression test of its own; the proof is the untracked forced
repro plus the sanitizer suite.

### 2.9 RT-04 / RT-05 / RT-06: the port edges

The fake backend runs a thread's entry inside `crug_pal_thread_join` and
its main-thread yield is a no-op, so `recv_into.c` cannot make progress
there. `check-RUNTIME` excludes that one program under `PAL=fake` and
`PAL.md` states why. The Windows monotonic clock multiplied the raw
performance counter by 10^9, which overflows a signed 64-bit value after
about 922 seconds at 10 MHz; it now converts whole seconds first and
saturates at `UINT64_MAX`. The fake clock counter is atomic because the
runtime reads it from several threads. The Windows source was reviewed
and built by the cross-target recipe but not executed on a Windows host;
that limit is recorded in section 6.

### 2.10 TL-01 / TL-02 / TL-03: c2crug

The fix-p2 commit deleted `_ControlScanError` and `_ControlScanStop` but
left every raise/except site, so any `#line` directive crashed c2crug with
`NameError`; because `--safe` wrote per file, it left a partially
migrated tree with no report. The classes are restored, `--safe` stages
the whole run and publishes it atomically only after the report is
complete, and a preprocessing failure leaves the tree untouched.

The 1.00-confidence "straight-line path" label was false for exits outside
`_EXIT_CALLS`: `pthread_exit`, `_Exit`, `siglongjmp`, `pthread_cancel`,
`thrd_exit` and noreturn wrappers. Those spellings are in the exit list
now, and a call to one of them makes the function ineligible. A `#line`
that names another file truncates the preprocessed view, so eligibility
degrades to raw with a reason that names the line directives instead of a
misleading proof. Regressions are the `test_exit_calls`,
`test_noreturn_exit` and `test_line_directives` cases in
`scripts/test-c2crug.py`.

### 2.11 TL-04 / TL-05 / TL-06: release provenance

`dist/` kept the artefacts of earlier versions while `SHA256SUMS`
covered only the current one. `scripts/clean-dist.sh` (and `make
dist-prune`) deletes other versions and rewrites the checksum file,
refusing to run when the current files do not verify. c2crug read its
version from a hard-coded `0.1.0`; it now reads `VERSION` and reports
0.2.4, and `test_version_matches_release` pins it.

SPDX now walks with `followlinks=False`, emits one `OTHER` entry per
symbolic link with its target in a comment, keeps the package verification
code over the regular files, and `check-packaging.sh` compares both sets
with the tarball. The Debian Maintainer comes from `DEBFULLNAME` and
`DEBEMAIL` when both are set, or a fixed release identity, never from the
build host's git author; `build-examples.sh` spells out the runtime
objects and passes flags through one documented split, so no path or flag
list is expanded unquoted.

### 2.12 TL-07: fuzzing the concurrency spellings

The parser target now carries the concurrency words in its dictionary and
two new seeds (`concurrency-syntax.c`, `concurrency-words.c`), so the
ownership-only target exercises `go`/`scope`/`chan`/`channel`/`send`/
`recv`/`select` as ordinary identifiers and as contextual productions
that must be rejected cleanly. The remaining audit suggestions (a c2crug
fuzz target, a channel-target fuzz arm) are not implemented in this batch
and are recorded in section 6; the batch does not claim them.

## 3. Decisions and adjudications

- **TYP-01 is accepted, not fixed.** The bounds check traps; only the
  constructor's length is unvalidated. Rejecting it would add a
  diagnostic contract to a function the audit itself marks SUSPECTED, so
  the decision is recorded and a future warning is left as residual work.
- **The do-while fix is a real join, not a blanket rejection.** The probe
  walk keeps the accepted loop forms and only makes the back edge visible;
  the conservative `do { } while (0)` answer is documented instead of
  special-cased for one body shape.
- **An escaped pointer is never un-escaped.** A killed escape would make
  the alias fact unsound again; the price is a conservative rejection
  (`crug-BORROW-024`) on code that really is disjoint.
- **Depth 32 fails closed.** A visited set would accept deeper acyclic
  types, but it also needs a cycle rule and a diagnostic; fail-closed is
  the smaller sound change and names the type in the error.
- **File-scope `own` is rejected, not implemented.** Implementing it
  means a translation-unit-wide ownership pass, which is a language
  change, not a defect fix. A function-local static `own` remains a
  documented limitation because its lifetime is not modelled either.
- **RT-03's natural window is not claimed.** The mechanism is proven at
  the code and forced-window level; a test that relies on a 2 ms window
  would be flaky, so the residual is a committed deterministic harness,
  not a fake assertion.
- **RT-05 is a source-level fix.** The Windows code was reviewed and
  cross-compiled, but this host cannot execute it; the gate's Windows leg
  remains the evidence path.
- **TL-01's atomic publication is the fix, not a retry.** A partial tree
  with no report is the dangerous outcome; `--safe` now publishes a
  complete run or nothing.
- **K-042 stays open (P2).** The diagnostic scripts still resolve `crugc`
  from `PATH` before the in-tree launcher. Every verification run in this
  batch used `PATH="$PWD/gcc:$PATH"`, as `scripts/gate.sh` does.
- **The gate text is re-baselined, not rewritten.** A-34 records the new
  counts and the checks that were re-executed; the historical phase text
  and A-33 stay unchanged.
- **The phase archives are not regenerated.** Re-archiving needs the
  packaging/container arm; the old archives remain immutable and A-34
  says so.

## 4. Verification matrix (c43060459 + fix-p3)

Run with `PATH="$PWD/gcc:$PATH"` (K-042) on the rebuilt `cc1`:

| Check | Result |
|---|---|
| `make check-DIAGNOSTICS` / `scripts/check-diagnostics.sh` | 137 registered, 137 referenced |
| `make check-OWNERSHIP` | 62 expected passes |
| `make check-BORROW` | `borrow-suite: 109 tests, 0 failures` |
| `make check-ALIAS_FACTS` | 42 DejaGnu cases |
| `make check-FFI` | `ffi-suite: 33 cases, 0 failures`; policy pins FFI-001..FFI-013 |
| `make check-CHANNELS` | `channels: 53 compiler test(s), 8 channel program(s), 0 failure(s)` |
| `make check-SEND_SYNC` | `send-sync: 26 compiler test(s), 0 failure(s)` |
| `make check-PAL` | `check-pal: fake and linux share 13 contract cases, 0 failures` |
| `make check-C2CRUG` | `test-c2crug: all 171 checks pass`; `c2crug --version` is 0.2.4 |
| `make -C libcrug check-sanitizer` | `tasks-sanitizer: ASan and UBSan clean over the suite, 100000 tasks, 200 scopes, 3000 group lifetimes and 20 held completion wakes` |
| `make check-MULTI_OS` | `multi-os: Linux/x86_64/sysv pal-contract 13 cases, tasks 17 checks, 3 channel program(s)` |
| `make check-DOCS` | 45 documents, 0 violations |
| `scripts/check-p0-open.sh` / `count-open-p1.sh --max=2` | 0 open P0; 0 open P1 |
| `scripts/check-regressions.sh` | every fixed P0/P1 has its committed regression (19 entries); RT-03 is P2 and has none |

`scripts/gate.sh --dry-run` for phases 5, 6, 7, 9, 11, 12, 13, 14, 15,
16, 17, 18, 24 and 25 parses every edited expectation; the phase archives
themselves are not regenerated.

## 5. Release state

`VERSION` and `gcc/crugc --version` still report 0.2.4; the language
version (0.1.0), the Crug and runtime ABIs (1), `.crugffi` (1.0) and the
PAL contract (1.2) are unchanged. The batch is `[Unreleased]`; there is
no 0.2.5 heading, no tag and no new artefact. The ledger records K-043 to
K-045 (the P0/P1 subset; the P2/P3 identifiers are in this batch's
`LEDGER.md`) and `docs/crug/EXIT-GATES.md` amendment A-34 re-baselines
the counts the batch moved.

## 6. Residual work

- K-042 (open P2): the diagnostic scripts should prefer the in-tree
  launcher or fail loudly instead of using an installed `crugc` from
  `PATH`.
- K-031 (open P2): a PAL 1.3 thread destructor is still needed before the
  legacy `crug_chan_recv_ptr` scratch can be released by a detached
  external thread.
- TYP-01: a future `-Wcrug` warning could reject a negative or oversized
  `slice` length at the call site.
- RT-03: a committed deterministic harness for the start/shutdown
  interstice would replace the untracked forced-pause repro.
- TL-07: a c2crug fuzz target and a channel-target fuzz arm remain
  unimplemented; this batch only widened the parser dictionary and seeds.
- The do-while exit join is conservative: `do { } while (0)` with a
  pre-loop owned value released in the body can report `crug-DROP-006`.
- A function-local static `own` is still accepted and not dropped; the
  limitation is documented in `OWNERSHIP.md`.
- A plain copy or reassignment of a thread-affine value is still not
  tracked; only a field or constructor store is (CONC-02).
- The AArch64 and Windows runtime legs were not executed on this host;
  the multi-OS gate remains their evidence path.
- The phase archives for the re-baselined gates have not been regenerated
  for this batch; re-archiving needs the packaging/container arm.
