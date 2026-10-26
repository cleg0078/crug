# Crug 0.2.0

Status: release 0.2.0, 2026-10-26, carrying the post-`v0.1.0-rc1` defect batch
in section 8. The qualification numbers in sections 3 and 5 are the ones the
Phase 25 gate (`docs/crug/EXIT-GATES.md`) recorded for the candidate, and every
threshold is owned by `docs/crug/RELEASE-POLICY.md`; section 8 says what the
batch changed.

## 1. What this candidate is

Crug adds Rust-style ownership, a borrow checker, deterministic drop, goroutines
with scopes and channels, safe core types and a `.crugffi` interoperability
contract to C, carried by a pinned GCC 16.2.0 baseline. Every feature is opt-in:
with no `-fcrug-*` flag the compiler produces the same objects as GCC 16.2, and
the upstream testsuite remains the compatibility floor.

`gcc/crugc --version` reports the six version axes separately: `crugc 0.2.0`,
`compiler: GCC 16.2.0`, `language version: 0.1.0`, `Crug ABI version: 1`,
`runtime ABI version: 1`, `FFI metadata version: 1.0`, `PAL version: 1.2`.

## 2. Highlights

- **Ownership and drop.** `own` declarations, `move`, `adopt`, `release`, `drop`,
  and automatic destruction on every exit path, with stable `crug-UAM-*`,
  `crug-OWN-*` and `crug-DROP-*` diagnostics.
- **Borrowing.** Non-lexical lifetimes, `ref`/`mut` qualifiers, borrow splitting
  and a depth-zero interprocedural policy (ADR 0019, ADR 0020).
- **Concurrency.** `go`, `scope`, `chan T`, `send`, `recv`, `close` and `select`
  over an M:N runtime with pinned tasks (ADR 0009 A-1), guard-page stacks sized
  from the backend page size, and Send/Sync inference (ADR 0026).
- **Safe types and ADTs.** `slice<T>`, `option<T>`, `result<T, E>`, bounds-checked
  indexing, payload enums and `match`, with the ownership of a payload following
  the value.
- **Interoperability.** `.crugffi` sidecars with owned, borrowed, raw, nullable,
  thread-affinity and callback clauses, validated at every inclusion site.
- **Tooling.** `c2crug` migration (analyse, safe rewrite, diff), strict mode,
  ten feature flags, SARIF diagnostics with stable rule ids and a checked
  fifty-class golden corpus.
- **Portability.** Linux x86-64 and AArch64, Windows x86-64 and macOS arm64 from
  one runtime core, one PAL contract per OS and one context switch per ABI
  (ADR 0012), with Linux kernel build qualification (ADR 0017).
- **Packaging.** A deterministic source/install tarball, an SPDX 2.3 SBOM, an
  x86-64 container image and build instructions for all three.

## 3. Qualification evidence

| Area | Evidence | Where |
|---|---|---|
| C compatibility | 224,481 expected passes, 84 allowlisted failures, byte-identical objects for the corpus | `docs/crug/GCC-BASELINE.md`, `docs/crug/baseline/` |
| Feature suites | ownership, drop, borrow, strict, safe types, FFI, goroutines, scopes, channels, Send/Sync | `make check-OWNERSHIP` and siblings |
| Zero cost | 12 comparisons, 0 added instructions, 0 added calls, identical optimised IR | `docs/crug/PERFORMANCE.md` |
| Plain-C parity | 114 source/option pairs byte-identical to unmodified GCC 16.2 | `bench/run.py --pristine` |
| Runtime | dispatch ratio 0.82 of pthread-per-task, 67.7 KiB per task against 8,032 KiB | `docs/crug/PERFORMANCE.md` |
| Channels | 197 ns per round trip, 42.5M elements/s, 4,224 bytes per channel | `docs/crug/PERFORMANCE.md` |
| Kernel | 2,980 translation units, 0 errors, 436 of 438 objects byte-identical (the two explained objects record the build timestamp and the compiler identification), boot to a freestanding `/init` | `docs/crug/KERNEL-BASELINE.md` |
| Ports | 12-case PAL contract plus 17 task checks on Linux x86-64/AArch64 and Windows x86-64, macOS in CI | `scripts/check-multi-os-ports.sh` |
| Fuzzing and sanitizers | six harnesses, 0 crashes, ASan/UBSan/LSan clean, TSan on the OS-thread channel arm | `docs/crug/FUZZING.md` |
| Defects | 0 open P0, 0 open P1 at this tag | `docs/crug/KNOWN-ISSUES.md` |

## 4. Install and verify

```sh
tar xzf crug-0.1.0-linux-x86_64.tar.gz
export PATH=$PWD/crug-0.1.0/bin:$PATH
crugc --version
crugc hello.c -o hello && ./hello
crugc -fcrug-ownership hello-own.c -o hello-own && ./hello-own
crugc -fcrug-goroutines -fcrug-channels -I/opt/crug/include/crug \
      -L/opt/crug/lib -lcrug -pthread runtime-sample.c -o runtime-sample
```

The `-L`/`-lcrug` pair follows the source file: a static archive is scanned when
it is seen, so a library named first leaves the `crug_*` runtime symbols
undefined. `docs/crug/BUILD-INSTRUCTIONS.md` covers the source build and the
container image (`crug/pkg:0.1.0`), and `packaging/smoke/` holds the three
programs above.

## 5. Known limitations

- No Windows x86-64 or macOS ARM64 binary package; those ports are qualified by
  cross-built suites and Wine or CI, and packaging for them stays open.
- The release artefacts are unsigned and carry no transparency record.
- The task pool is about fourteen times slower than Crug's own per-task spawn:
  every task reserves and commits a 64 KiB guarded stack by design (ADR 0008).
- Fuzzing is bounded by a budget and a nightly window; it is evidence, not a
  proof of absence.
- Kernel code is plain GNU C: no ownership, drop, borrow or race analysis
  applies inside the kernel (ADR 0017).

## 6. Compatibility and support

`docs/crug/COMPATIBILITY-POLICY.md` defines language, Crug ABI, `.crugffi`,
libcrug ABI, feature-deprecation, GCC-baseline and platform policy. Version
compatibility is checked mechanically: `scripts/check-diagnostics.sh` keeps the
diagnostic registry append-only, `scripts/check-docs.sh` keeps the documentation
contract, and `scripts/check-fuzz-nightly.sh` keeps the reliability window.

## 7. What is not in this candidate

No new syntax, no architecture change, no additional platform, no benchmark
target change and no documentation redesign are allowed between this tag and
1.0 (section 30 RC rules). The named open items are the Windows and macOS
binary packages, artefact signing, reproducible image builds and the
post-1.0 feature list in `docs/crug/COMPATIBILITY-POLICY.md`.

## 8. Post-rc1 fixes

This section records the bug-fix batch after `v0.1.0-rc1` on top of
`f86b7df`, released as 0.2.0; no `rc2` tag is cut and the candidate claims in
sections 1-7 are otherwise unchanged. The change implements the external audit of
2026-10-23 (twenty-one defects, `docs/crug/KNOWN-ISSUES.md` K-003 to K-023):

- runtime: a capacity-0 send now completes on its own ticket instead of the
  shared slot being empty, so two senders cannot steal each other's
  completion; channel receive uses per-task scratch instead of thread-local
  storage; the scheduler admits a spawn atomically and refuses one during
  shutdown (fatal `crug-CONC-029`); `go` frames are size-checked
  (`crug-CONC-028`) and aligned; shutdown has a checked, restartable form
  (`crug_task_runtime_try_shutdown`: 0 ok, 1 not started, 2 called from a
  task, 3 PAL join failure) while `crug_task_runtime_shutdown` keeps its
  `void` contract, and a failed join is sticky and does not free the handle;
- channels: the claim API behind `select` (`crug_chan_recv_claim`,
  `crug_chan_send_claim`, `crug_chan_send_owned_claim`: 1 accepted, 0 would
  wait, -1 closed) and ticket-based completion replace the polling protocol;
  claim send and claim receive cannot rendezvous on capacity 0, so a claim
  send pairs with a blocking receive and a claim receive with a blocking or
  buffered send;
- borrow and alias: the borrow lattice is joined at CFG joins, empty owned
  reads are `crug-UAM-008`, and an unproven disjoint root or an indirect
  call keeps facts unproven (`crug-BORROW-024`) instead of emitting
  `restrict`;
- FFI: `crug-FFI-011` now requires a real function, a non-variadic
  prototype and exactly one pointer parameter matching the pointee (the
  return type is ignored, so `fclose` remains a valid destructor); `argN`
  clauses are validated against the same shape;
- parser: the contextual-qualifier lookahead buffer is 16 tokens with a
  checked bound (four chained qualifiers used to overwrite a four-token
  buffer);
- c2crug: only a straight-line unconditional free proves safety
  (confidence 1.00); a null guard proves 0.80 only when it controls an early
  exit that dominates the release; no transform is applied without proof, and
  multi-declarator top-level declarations stay raw;
- gates: `scripts/gate.sh` fails when a CHECK was not executed and
  `scripts/check-gate-archives.sh` requires every recorded check, each with a
  negative self-test; `make check-P1` runs the fix regressions, and the
  coverage provenance scripts and weekly CI job are new (the archive itself is
  not regenerated, see `TESTING.md` section 7).

The Crug runtime ABI stays 1. The PAL contract moves to 1.2 because
`crug_pal_thread_join` returns `int` (ADR 0011 A-2); the four backends and
the thirteen-case contract test were updated together.

Known limitations of these fixes, stated rather than hidden:

- a select send arm's value expression is evaluated on every claim pass and
  again in the closed-send abort path (ADR 0025 D5 amendment), so it must be
  side-effect-free apart from the move; the compiler does not diagnose a
  mutation there;
- a select arm that sends an owned value is not transferred, so the ownership
  pass can report a conservative `OWN-007` or `DROP-006`;
- claim send and claim receive cannot rendezvous on a capacity-0 channel, and
  the coverage archive for the fix commit is not regenerated;
- `scripts/gate.sh` cannot replay checks whose fixtures were removed; the
  three accepted-gap archives in A-31 are replayed and printed, not green.
