# fix-p2: analysis, tasks and execution record

Source: the audit drop in `spec/fix-p2/` (French reports: `RAPPORT.md`,
`RAPPORT (1).md`, `RAPPORT (2).md`, `MATRICE.md`, `MATRICE (1).md`,
`MANIFESTE.md`, `MANIFESTE (1).md`, `COUVERTURE.md`, `REVALIDATION.md`,
`SECURITE.md`, `PERFORMANCE.md`, `README.md`). Every finding below was
reproduced on this tree before it was fixed; the fix, its regression and the
ledger row are in the last three columns.

Base commit `c0dcb18` (Crug 0.2.3), batch released as 0.2.4 on 2026-10-28.

## 1. How the batch was run

The findings were split into seventeen board tasks under three rules: one task
per independent root cause, one owner per task, and no owner touching another
owner's files. Two findings share a root cause and therefore one task
(F-01 and CR-02 both come from the ownership walk treating a condition as a
sequence); one finding is documentation, not code (F-03); one is a follow-up
that needs a PAL contract bump and was deliberately deferred (the thread-local
receive buffer, C-02). Verification tasks were added when the work exposed a
gap: T4b for the front-end half of the receive lowering, T15 for the FFI parser
crash the fuzz harness found during the batch, T16 for an ownership false
rejection the receive rework exposed, T17 for a committed run-time alignment
regression.

| Task | Finding | Area | Outcome |
|---|---|---|---|
| T1 | - | build | in-tree toolchain restored (`build/obj/gcc/xgcc`, cc1 484316152 bytes) and a frozen pre-fix driver kept in `build/prefix-obj` for before/after evidence |
| T2 | F-01, CR-02 | ownership | conditional, `&&` and `||` arms are alternatives: each arm is walked from a clone of the pre-condition state and joined; `drop` on one arm is accepted, a leak or a double release is still reported |
| T3 | CR-02 | ownership | a `switch` body is a set of alternatives, not a sequence; fallthrough chains and an all-breaking `default` join correctly |
| T4 | C-01, A-01, C-02 | runtime | `crug_chan_recv_into` / `_owned` plus a bounded, reusable receive scratch (64 KiB cap); legacy pointer forms documented |
| T5 | R-01, R-02, S-01 | runtime | scope `in_flight` handshake, atomic `queue_peak`, start/shutdown handshake |
| T6 | CR-01, F-02, F-03, F-04 | ffi | dangling `argN` string fixed, NUL byte is `crug-FFI-012`, 10,000-entry cap, `owned` optional on `returns` (docs) |
| T7 | S-02, C2-A/B/C, R4-TOOL-003 | tooling | `c2crug --safe` decides eligibility on preprocessed source, validates the sidecar header, caches per-function facts |
| T8 | CR-03 | scripts | coverage provenance, `gate.sh` marker escaping, CI run selection |
| T13 | C-01 | front end | `recv` lowers to the new entry points into caller-owned storage |
| T15 | (fuzz) | ffi | truncated `.crugffi` no longer indexes past the token vector: `crug-FFI-003` at the right column |
| T16 | (C-01 fallout) | ownership | `drop` of a value received from an owning channel is accepted; an unreleased one is still `crug-OWN-007` |
| T17 | C-01 | channels | committed UBSan alignment program and gate arm |
| T9 | - | docs | the twelve stale doc statements the audit flagged |
| T10 | - | ledger/gates | K-024 to K-042 and amendment A-33 |
| T11 | - | release | 0.2.4 artefacts |
| T12 | - | verification | this document and the matrix in section 4 |

## 2. Findings, fixes and evidence

### 2.1 F-01: `drop` inside a conditional arm is lost or falsely rejected

`c ? drop (p) : 0;` walked the then-arm and then the else-arm from the *then*
state, so the release seen in one arm leaked into the other. Now each arm
starts from a clone of the pre-condition environment including the borrow and
root bases, and the two results are joined
(`join_borrow_alternatives` / `join_ptr_root_alternatives`,
`merge_into`). `cond-expr-drop.c`, `cond-expr-and-or-drop.c` and
`cond-expr-move-both.c` are accepted; `cond-expr-double-use.c` still reports
`crug-UAM-001`. `drop (x)` on a pointer variable now stores NULL after the
destructor (`drop_clears_variable()`, `crug-ownership.cc`), which is what makes
a repeated drop site legal; the drop lowering is guarded (`if (decl != 0) dtor
(decl)`), so a repeated site never calls a destructor with NULL. Ledger K-024,
K-041.

### 2.2 CR-02: a `switch` body is not a sequence

Each `case` is entered from the innermost non-loop target entry, so a variable
moved in one arm is not "moved" in the next; fallthrough chains and a
`default` whose arms all break join correctly.
`switch-drop-default.c`, `switch-drop-fallthrough.c` and
`switch-move-arms.c` cover it. Ledger K-025.

### 2.3 C-01: a receive of an over-aligned element is misaligned

The audit's witness loaded a `_Alignas(64)` element from the 16-byte scratch:
UBSan reported `load of misaligned address ... for type 'aligned_word', which
requires 64 byte alignment` 8/8 runs before, 0/8 after. The fix is structural
rather than a bigger alignment: `recv` now writes into storage the caller owns.
`crug_chan_recv_into (chan, dst, size)` and `_owned` check `size` against the
element size, require aligned storage, return 1 for a value and 0 for a closed,
drained channel (zeroing `dst`), and never touch the shared scratch. The front
end lowers a non-VLA `recv` to a `TARGET_EXPR` temp via
`crug_chan_recv_into` (`crug-chan.cc`); the VLA and error paths keep the
pointer form. Fixtures: `channels/recv-over-aligned.c` (dg),
`libcrug/tests/channels/recv-align.c` plus its UBSan arm in
`scripts/check-channels.sh`, and `libcrug/tests/runtime/recv_into.c`. No
public symbol was removed; the runtime ABI stays 1. Ledger K-030.

### 2.4 C-02 and A-01: the shared receive buffer

The legacy pointer forms kept a per-thread scratch that a task could hold
across a block (A-01) and that an external thread retains until it exits
(C-02). A-01 is fixed: the scratch is per task, capped at 64 KiB, freed with
the task above that size (`recv_scratch_big.c` asserts the cap; peak RSS fell
from 4,262,112 bytes to a few kilobytes). C-02 is *accepted and documented*
rather than fixed: freeing it needs a PAL thread destructor, i.e. a contract
1.3 with ripple into ADR 0011, `PAL.md`, the fake/Windows backends and the
multi-OS gate, which is out of scope for a defect batch. `chan.h` states the
retention; the follow-up is board task T14 and ledger K-031 (open P2).

### 2.5 R-01, R-02 and S-01: three runtime races

- R-01: a scope that completed while its parent waited could lose the wake.
  Pre-fix ASan: `heap-use-after-free ... #0 crug_pal_wake_all
  libcrug/tests/runtime/scope_wake_race.c:60`; post-fix clean. The scope now
  counts children and publishes `in_flight` before it waits.
- R-02: `queue_peak` was read and written without atomics. Pre-fix TSan exited
  66 on `task.c:595` vs `task.c:1168`; post-fix `stats-race: samples=14089 ...
  reader_errors=0`.
- S-01: a start that overlapped a shutdown could leave a half-built worker set.
  Pre-fix `startup-shutdown-race: blocked_create=1 status=3 null_join=1`;
  post-fix `status=0 null_join=0`. `crug_task_runtime_try_shutdown` keeps its
  0/1/2/3 contract.

Three new Linux programs pin them (`scope_wake_race`, `stats_race`,
`startup_shutdown_race`), plus `startup_shutdown_handshake`,
`start_then_shutdown` and `linux_spawn_shutdown`; the scope fixture
`inside-task.c` was made deterministic after it turned out to race the shutdown
on unmodified 0.2.3 as well. Ledger K-033 to K-035.

### 2.6 CR-01, F-02, F-03 and F-04: FFI metadata

- CR-01: `argN` stored a `std::string` that could dangle (the parser hoisted a
  function-scope reference); fixed and covered by `ffi/metadata/argref-space`.
- F-02: a NUL byte inside the sidecar tokenised the rest as garbage; it now
  reports `crug-FFI-012` at the right column before tokenising.
- F-03: the audit read `returns int` as missing `owned`. The parser is right:
  ADR 0007 D2 makes `owned` optional, so the *documentation* was amended
  ("A-3: `owned` is optional on `returns`") and a fixture
  (`ffi/metadata/returns-unowned`) pins it.
- F-04: an unbounded declaration count became `crug-FFI-010` at 10,000 entries
  per file.
- The fuzz harness then found a real crash: a truncated `fn` clause indexed
  past the token vector (`std::vector::operator[]` assertion, `parse_argref`
  `crug-ffi.cc:350`). It is pre-existing latent UB, not a regression; the parser
  now guards every token read past the end and reports `crug-FFI-003`.

The suite grew from 28 to 30 cases and the policy pin lists all twelve codes.
Ledger K-026 to K-029.

### 2.7 S-02 and C2-A/B/C: `c2crug --safe` judged text it did not compile

The rewriter decided eligibility on the raw tokens, so a macro could hide an
aliasing free or a second exit. `--safe` now preprocesses (`cc -E -x c`, with
`--cpp` / `--cpp-arg`), maps the result back through the `#line` markers, and
accepts a release only when the declaration prefix and the release span are
token-identical in both texts and no exit token was added or removed. The
sidecar must begin with `crugffi <major>.<minor>;` (major 1), and per-function
facts are cached. Four witnesses (`hidden-alias`, `nested-return`,
`free-redefinition`, `same-line-exits`) are all raw with confidence 0.00 after
the fix; `hidden-alias` was applied before and its program failed under ASan
(`heap-use-after-free ... READ of size 4 ... in main ... freed by ... in take`).
The same-line exit proof is the R4-TOOL-003 regression. Timing:
`bench-c2crug.py --pairs 800` 7.33 s before vs 0.73 s after (10.06x). Ledger
K-037 to K-040.

### 2.8 CR-03: coverage numbers without provenance

`scripts/run-coverage.sh` accepted inputs that did not say which commit they
measured. It now requires a top-level `commit` in the measurement, the
profiles and the corpus (exit 2 when missing, exit 1 on mismatch) and has a
seven-case `--self-test`. In the same pass `gate.sh` stopped treating a marker
as a regular expression (ERE metacharacters are escaped, N-boundary
placeholders keep their spaces) and `check-ci-gate.sh` selects the newest run
of `push`/`pull_request`/`workflow_dispatch` only. All three have
`--self-test`; `make check-P1` runs them. Ledger K-036.

### 2.9 T16: a false `crug-OWN-007` on an owned receive

Reworking `recv` exposed a dead check: `crug-ownership.cc:3196` required a
`CALL_EXPR` where the initializer is an `INDIRECT_REF`/`TARGET_EXPR`, so
`own int *v = recv (c); drop (v);` was rejected. A received owned value is now
modelled as an adopted value the receiver may release without a destructor (the
element type erased it), and `drop` is accepted while an unreleased value and a
double drop are still diagnosed. Ledger K-041.

## 3. Decisions and adjudications

- **F-03 is a documentation defect, not a parser defect.** The audit expected a
  required `owned`; ADR 0007 D2 says otherwise. The parser was left alone and
  ADR 0007 gained amendment A-3.
- **C-02 is accepted, not fixed.** A PAL thread destructor is a contract bump;
  the batch documents the retention and files T14.
- **The receive fix is structural, not an alignment bump.** Copying into
  caller-owned storage removes the whole class (alignment, retention, aliasing
  with a blocked receiver) instead of widening the scratch.
- **`c2crug` may not claim a proof it cannot back.** Eligibility is decided on
  the preprocessed translation unit, and the conservative answer stays raw.
- **A mixed conditional initializer stays diagnosed**:
  `own int *q = c ? move (p) : (int *) 0;` still reports `crug-OWN-007`
  because one arm would leave the value owned and the other not. Known
  residual, recorded rather than papered over.
- **The `PATH` resolution order stays as it is** for this batch. The diagnostic
  scripts prefer `crugc` from `PATH` over the in-tree launcher, so on this
  machine they validated the *installed* 0.2.3 compiler until `PATH` was
  prefixed with `gcc/` (which is what `scripts/gate.sh` always did). Every
  verification run below used `PATH=$PWD/gcc:$PATH`. K-042 open P2.

## 4. Verification matrix (0.2.4 tree)

Run with `PATH=$PWD/gcc:$PATH` (K-042) on the rebuilt `cc1`:

| Check | Result |
|---|---|
| `make check-OWNERSHIP` | `# of expected passes 42` |
| `make check-BORROW` / `check-ALIAS_FACTS` | 109 / 37 |
| `make check-STRICT_UNSAFE` / `check-SAFE_TYPES` | 21 / 77 |
| `make check-GRAMMAR_DG` / `check-SEND_SYNC` | 22 / 14, 0 failure(s) |
| `make check-CHANNELS` | `channels: 49 compiler test(s), 8 channel program(s), 0 failure(s)` (49 DejaGnu assertions over 30 sources) |
| `make check-GOROUTINES` | `31 compiler test(s), 5 task program(s), 100000 tasks on 4 worker threads, 0 failure(s)` |
| `make check-SCOPE` | `23 compiler test(s), 8 group program(s), 0 leaked group(s), 0 failure(s)` |
| `make check-FFI` | `ffi-suite: 30 cases, 0 failures`; policy pins FFI-001..FFI-012 |
| `make check-PAL` | fake and linux share 13 contract cases, 0 failures; lint sees 7 files, 0 OS-primitive names |
| `make check-MULTI_OS` | pal-contract 13 cases, tasks 17 checks, 3 channel programs |
| `make -C libcrug PAL=linux check-tasks` | `runtime[linux]: 15 programs, 2 fatal programs`; `tasks: 17 checks, 0 failed`; stress 100000/100000, lost 0, corrupt 0 |
| `make check-SANITIZER` | ASan/UBSan clean over the suite, 100000 tasks, 200 scopes |
| `make check-C2CRUG` | 140 checks; `6 own applied, 16 raw preserved, 8/8 programs pass`; `--diff` 12 of 491 lines (2.4%) |
| `make check-C2CRUG_PROOFS` | 8 proof fixtures classified, compiled and run as expected |
| `make check-P1` | 22 checks passed (includes the three new self-tests) |
| `make check-C_COMPAT` | `diff-cc: 30 files, 0 differing` at -O0/-O2/-Os, hidden state 0, 0 FAIL |
| `make check-DIAGNOSTICS` | 134 registered, 134 referenced |
| `make check-DOCS` | 45 documents, 0 violations |
| `scripts/check-p0-open.sh` / `count-open-p1.sh --max=2` | 0 open P0, 0 open P1 |
| `scripts/check-release.sh` | 25 checks, 0 problems |
| `make check-launcher` / `check-DEB` | 5 cases 0 failures / 0 problems |

## 5. Release state

`VERSION` and `crugc --version` report `0.2.4`; the language version (0.1.0),
the Crug and runtime ABIs (1), `.crugffi` (1.0) and the PAL contract (1.2) are
unchanged. `make package` produced:

    package: crug-0.2.4-linux-x86_64.tar.gz 143312870 bytes, 971 files
    deb: dist/crug_0.2.4-1_amd64.deb 93765600 bytes, 976 files, installed-size 449420 KiB
    package: SHA256SUMS: 5 artefact(s)

`CHANGELOG.md` carries `[0.2.4] - 2026-10-28`; `docs/crug/RELEASE-NOTES.md`,
`docs/crug/BUILD-INSTRUCTIONS.md`, `README.md` and `packaging/README.md` are
updated to the new version and the transcript above. The ledger records
K-024 to K-042 and `docs/crug/EXIT-GATES.md` amendment A-33 re-baselines the
counts the batch moved.

## 6. Residual work

- K-042: the diagnostic scripts should prefer the in-tree launcher (or fail
  loudly) instead of silently using an installed `crugc` from `PATH`.
- K-031 / T14: a PAL 1.3 thread destructor would let the legacy
  `crug_chan_recv_ptr` scratch be released by a detached external thread.
- One mixed-arm conditional initializer still reports `crug-OWN-007` (see
  section 3); the all-owned and none-owned forms are accepted.
- The Phase 14 and Phase 24 gate archives have not been regenerated for this
  batch; `scripts/check-gate-archives.sh` still reports the pre-existing
  phase-24 marker gap, and re-archiving needs the Docker packaging arm.
