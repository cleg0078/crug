# fix-p3 identifiers and evidence

The audit drop is in `specs/fix-p3/`. It reports 22 confirmed findings
and three observations it marks SUSPECTED or OBSERVÉ (`OWN-05`,
`TYP-01`, `RT-03`); this table carries the 25 identifiers, all
adjudicated. Every fix is on `fix-p3` on top of `c43060459` (Crug
0.2.4), and every regression path is relative to the repository root.

| Identifier | Severity | Area | Status | Fix commit | Regression / evidence |
|---|---|---|---|---|---|---|
| OWN-01 | P0 | compiler | fixed | `6126fcb71` | `gcc/testsuite/crug.dg/ownership/do-while-double-move.c`; a move in a do-while body was invisible to the back edge and double-freed |
| OWN-02 / OWN-02b | P1 | compiler | fixed | `6126fcb71` | `gcc/testsuite/crug.dg/ownership/do-while-leak.c`, `do-while-use-after-drop.c`; the adopt overwrite leaked and the release read was a use-after-release |
| OWN-03 | P1 | compiler | fixed | `25a0a2edc` | `gcc/testsuite/crug.dg/alias/global-escape.c`, `global-store-field.c`; the escaped lender now reports `crug-BORROW-024` |
| OWN-04 | P3 | compiler | fixed | `31a4cc547` | `gcc/testsuite/crug.dg/ownership/own-file-scope.c`, `own-file-scope-static.c`; file-scope `own` is `crug-OWN-014` |
| OWN-05 | P3 | compiler | fixed | `31a4cc547`, `fb8ef616a` | `gcc/testsuite/crug.dg/ownership/own-array-of-pointers.c`, `own-array-of-records.c`; an `own` array is `crug-TYPE-015` |
| FFI-01 | P2 | ffi | fixed | `33348caaf` | `gcc/testsuite/crug.dg/ffi/metadata/eof-type.c`, `eof-opaque.c`, `eof-partial.c`; EOF inside a declaration is `crug-FFI-013` |
| FFI-02 | P3 | ffi | fixed | `33348caaf` | `scripts/check-diagnostic-sarif.sh`; parse-time sidecar errors carry the FFI ruleId and helpUri |
| CONC-01 | P2 | compiler | fixed | `90bcffb44` | `gcc/testsuite/crug.dg/send-sync/chain-sync-33.c`, `chain-send-33.c`; depth 32 now fails closed |
| CONC-02 | P3 | compiler | fixed | `ce5d95546` | `gcc/testsuite/crug.dg/send-sync/affine-field.c`, `affine-field-clean.c`, `affine-field-chan.c`; member store records affinity |
| CONC-03 | P3 | docs | fixed | `dce29e744` | `gcc/testsuite/crug.dg/channels/select-break.c`, `select-return.c`; `break` in a select arm is a parse error |
| TYP-01 | P3 | compiler | accepted | - | by design: the bounds check traps; an optional length warning is residual |
| RT-01 | P2 | runtime | fixed | `27b99d64e` | `libcrug/tests/runtime/scope_free_race.c`; the `in_flight` handshake is live |
| RT-02 | P3 | runtime | fixed | `a8507bda1` | `libcrug/tests/runtime/scope_wake_race.c`; the seam arms after runtime start |
| RT-03 | P2 | runtime | fixed | `27b99d64e` | forced-pause repro is clean; the natural window was never observed, so no committed test |
| RT-04 | P3 | runtime | fixed | `a8507bda1` | `libcrug/tests/runtime/recv_into.c` is excluded under `PAL=fake` and `PAL.md` says why |
| RT-05 | P3 | runtime | fixed | `a8507bda1` | Windows clock converts seconds first and saturates; not executed on a Windows host |
| RT-06 | P3 | runtime | fixed | `a8507bda1` | fake PAL clock counter is atomic |
| TL-01 | P2 | ci | fixed | `6222b9e46` | `scripts/test-c2crug.py` `test_line_directives`; scanner restored and `--safe` publishes atomically |
| TL-02 | P3 | ci | fixed | `6222b9e46` | `scripts/test-c2crug.py` `test_exit_calls`, `test_noreturn_exit`; exit spellings widened |
| TL-03 | P3 | ci | fixed | `6222b9e46` | `scripts/test-c2crug.py` `test_line_directives`; cross-file `#line` is raw with a named reason |
| TL-04 | P3 | ci | fixed | `89b2beb9f`, `e00af704a` | `scripts/clean-dist.sh`, `make dist-prune`; c2crug reads `VERSION` and `test_version_matches_release` pins it |
| TL-05 | P3 | ci | fixed | `999c483c8` | `scripts/check-packaging.sh`; SPDX symbolic links and package verification code |
| TL-06 | P3 | ci | fixed | `e00af704a` | Maintainer from `DEBFULLNAME`/`DEBEMAIL` or a fixed identity; build-examples flags quoted |
| TL-07 | P3 | ci | fixed (scoped) | `6401377bb` | `fuzz/compiler/corpus/parser/concurrency-syntax.c`, `concurrency-words.c`; c2crug and channel-target fuzz arms remain residual |

## Verification

See `spec/fix-p3/PLAN.md` section 4. The counts A-34 pins are the output
of the checks as run on this tree; the ledger rows above name the
regression that would fail if the corresponding mechanism returned.
