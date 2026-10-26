# Compatibility Policy

Status: Phase 23 ratification, 2026-10-20. This document states what a Crug
user may rely on and what is still allowed to move. It is the promise side of
the project: `docs/crug/RELEASE-POLICY.md` fixes the numbers the gates
enforce, `docs/crug/ABI.md` fixes the object-level facts, and this file fixes
how long a fact stays true.

The rule behind every section is the same. A promise is named here, it is
tested by a named check, and changing it takes an amendment row at the end of
this file plus a rerun of the phase gate that cites the check. Pre-1.0 the
project may still break a promise, but it may not do so silently: the
amendment row is the record, and a released 1.0 may only break one through a
new major version.

## 1. Language versioning

Crug's language version in this tree is **1**, and it is **pre-1.0**: the
compiler is qualified for the surface this repository implements, not yet for
stability of that surface.

- The predefined macro `__CRUG__` is a presence marker (`__CRUG__=1`). It
  says "this compiler has the Crug extensions", not which version of them.
  There is no `__CRUG_LANGUAGE_VERSION__` and no `-fcrug-language=` switch
  in v1; a program cannot yet ask for an older dialect. That selector is
  future work if it is wanted, not a v1 feature.
- Each production is behind its own flag (`docs/crug/FLAGS.md` section 1).
  With all flags off the compiler is upstream GCC 16.2: same macros, same
  parses, same object code. That identity is the Phase 1 check
  (`scripts/diff-cc.sh`, `make check-C_COMPAT`) and it is a promise, not a
  convenience.
- Within the pre-1.0 series a change that accepts less than it used to, or
  that changes the meaning of an accepted program, needs an ADR or an
  amendment to the ADR that defined the construct, an amendment row here, and
  a rerun of the gate that cites it. A change that only accepts more is
  ordinary feature work and still needs its phase evidence.
- At and after 1.0 the same rule applies with a stronger consequence: a
  breaking language change requires a new major language version, a migration
  note, and a released compatibility statement. The compiler does not
  currently implement versioned language selection, so a post-1.0 major
  version would have to add it; that is a future design decision, not a
  promise made now.

## 2. Crug ABI versioning

Crug-native types have their own ABI version, independent of the C ABI of the
target and of the runtime ABI. The registry is `docs/crug/ABI.md` section 3.

- **Crug-native ABI version 1** covers the five C-ABI-transparent rows and
  the frozen layouts: `slice<T>` (pointer then `size_t`, 16/8 on
  x86-64), `option<T>` (tag then payload), `result<T,E>` (tag then a
  union), the payload enum (record with a tag and a union of payloads) and
  `chan T` (one pointer whatever the element type is). The `_Static_assert`
  fixtures named in `ABI.md` section 3 measure them on every run.
- A change to a recorded size, alignment, offset, field name or tag value is
  an **ABI change**. It bumps the Crug-native ABI version, adds a row to
  `ABI.md` section 8, and needs a migration note.
- Version **0** is the pre-stable value. `crugc --version` prints the compiler,
  language, Crug ABI, runtime ABI, FFI metadata and PAL versions separately
  (the Phase 24 packaging check); a program or installer may parse those
  lines.
- Adding a new Crug-native type does not change version 1. The version changes
  only when an existing row moves.

## 3. .crugffi versioning

A `.crugffi` sidecar is a versioned text contract; ADR 0007 D6 is canonical
and `docs/crug/FFI.md` is the user guide.

- The header is `crugffi 1.0;`. The compiler defines
  `__CRUG_FFI_VERSION__` to `MAJOR * 100 + MINOR`, so v1.0 is `100`. The
  value is a compile-time fact; no loader reads a sidecar at run time.
- A file whose major version is not 1 is rejected, and an unknown clause in a
  minor version is rejected: there is no best-effort parsing of semantics the
  compiler does not know. Adding a clause with new meaning is a major version
  and a new ADR.
- The same contract can be named explicitly (`-fcrug-ffi=<file>`, repeatable,
  merged in command-line order) or discovered next to a quoted include plus
  `CRUG_FFI_PATH` (`-fcrug-ffi-discover`). Under `-fcrug-strict`
  discovery steps 2-4 are disabled, so no file appearing next to a header can
  change a program's ownership semantics without being named.
- A sidecar's claims are a library contract, not a compiler guarantee: if it
  says a function returns owned and the function returns an interior pointer,
  the program can double-free. Review of the shipped file is the mitigation,
  and that is why the format is closed, diffable and versioned.

## 4. libcrug ABI policy

The runtime is a separate deliverable with its own versions.

- **Runtime ABI version 1** is the signature and structure surface of
  `libcrug/core/task.h` (Phase 14) plus the additive Phase 15 scope entry
  points (`crug_scope_enter`, `crug_scope_leave`,
  `crug_task_spawn_scoped`) and the Phase 16 channel entry points
  (`crug_chan_make`, `crug_chan_send`, `crug_chan_recv_ptr`,
  `crug_chan_close` and the select probes). The versioned surface includes
  the three task states and the `struct crug_task_stats`,
  `struct crug_scope_stats` and `struct crug_chan_stats` field sets.
  Additions are additive: a program that uses no `scope` and no channel never
  references the newer symbols.
- **PAL contract version 1.1**: `libcrug/pal/include/crug_pal.h` exports
  `CRUG_PAL_VERSION_MAJOR` (1), `CRUG_PAL_VERSION_MINOR` (1) and
  `CRUG_PAL_ABI_VERSION`. Version 1.1 added
  `crug_pal_page_size`, which the task runtime now uses to size its stack
  guard (ADR 0011 amendment A-1). A backend implements the whole contract; it
  never adds a function the core must know about.
- The runtime ABI is OS- and architecture-independent (`docs/crug/ABI.md`
  section 6): Linux x86-64 and AArch64, Windows x86-64 and macOS ARM64 expose
  the same signatures, and a port adds a backend without changing them.
- v1 ships static archives only, built from source, with no symbol versioning
  and no shared-library ABI. Removing a symbol, changing a signature, changing
  a structure layout or changing a documented invariant is a new major runtime
  ABI version and an ADR. A new backend or a new additive entry point is a
  minor version.

## 5. Feature deprecation

No Crug feature, option or diagnostic code has been deprecated. The rules are
fixed now so the first deprecation is not improvised.

- A deprecation starts with a warning carrying a stable code registered in
  `docs/crug/DIAGNOSTICS.md`. Codes are append-only and are never reused
  (`DIAGNOSTICS.md` section 7).
- The warning stays for at least one released minor version before the
  construct or option is removed, and removal happens only in a major version.
  The removal is recorded in this file and in the ADR that defined the
  construct.
- A deprecated construct keeps its meaning and its ABI until removal. "Deprecated"
  may not quietly change lowering, diagnostics or ownership behaviour.
- A `.crugffi` clause, a PAL function or a runtime symbol is deprecated by
  the same rule, with the version number of the owning surface.

## 6. GCC baseline policy

The compiler baseline is pinned, not floating: GCC **16.2.0**, imported as one
commit tagged `releases/gcc-16.2`, with the tarball digest recorded in
`docs/crug/GCC-BASELINE.md` section 1 and `gcc-baseline.sha512`.

- Only this baseline is supported. The fork delta against it is measured by
  `scripts/fork-delta.sh` (9 modified upstream files, 32 hunks, 495
  insertions and 6 deletions, plus the new `gcc/c/crug-*.cc` modules) and
  published in `docs/crug/GCC-UPGRADE.md`.
- ADR 0022 (Proposed) classifies a baseline change as a re-pin (P), a minor
  upgrade (M) or a major upgrade (J), and fixes the evidence each needs; the
  31-check acceptance matrix is `scripts/verify-upgrade.sh`. The drift
  checker `scripts/check-gcc-drift.sh` must report no unknown drift before a
  merge.
- An upgrade changes the pin, the import tag and the version the build
  reports together; `make xgcc-version` must print the version the pin
  names. `docs/crug/GCC-BASELINE.md` section 6 is the update procedure and
  section 7 the upgrade pointer.
- Using a different host compiler to build the fork (a distribution GCC, for
  example) is allowed by the build instructions and is not a supported
  baseline: object identity is promised only against objects produced by the
  pinned compiler.

## 7. Supported platforms

The v1 matrix is the one `docs/crug/PORTABILITY.md` section 2 measures.

| Host | State | Promise |
|---|---|---|
| Linux x86-64 | supported | the primary target for every gate |
| Linux AArch64 | supported | native CI job; the same runtime ABI |
| Windows x86-64 | supported | MinGW cross-build and Wine run; the same runtime ABI |
| macOS ARM64 | supported | macOS CI job; the same runtime ABI |
| macOS x86-64 | builds, not gated | the same sources and Darwin symbol names |
| no OS (`pal/fake`) | contract backend only | not a product backend |

- Supported means the compiler builds and the runtime passes its contract,
  task and channel suites there. `make check-MULTI_OS` is the local check and
  `.github/workflows/multi-os.yml` the CI one.
- Not supported in v1: BSD systems, Windows ARM64, bare metal and RISC-V.
  They are post-1.0 portability work (roadmap section 32), not a v1 promise.
- Kernel targets are narrower than user-space targets: x86-64 is the primary
  kernel target, and AArch64 kernel support is a compile-only object subset
  (section 9).
- The compiler itself is built from source with a host toolchain listed in
  `docs/crug/GCC-BASELINE.md` section 2; no root privileges are required.

## 8. Supported C dialects

Crug is a set of dialect extensions selected by flags, not a separate
`-std` value in v1.

- Default dialect: GNU C23. `crugc` with no options predefines
  `__STDC_VERSION__` as `202311L`, the same value upstream GCC 16.2
  predefines.
- The Crug productions are accepted in GNU C mode when their flag is on, and
  the plain-C compatibility suite (`make check-C_COMPAT`, the 30-program
  corpus, `scripts/diff-cc.sh`) proves that the same input produces the same
  object with the flags off. The GNU C compatibility check
  (`scripts/check-gnu-c-compat.sh`) covers 50 further translation units.
- Not supported in v1: Crug constructs in C++ or Objective-C, a `-std=crug`
  dialect name, and mixing Crug constructs across translation units with
  different feature flags. A translation unit's ownership and borrow checks are
  a property of that translation unit and of its `.crugffi` contracts.
- `-fcrug-kernel` is kernel-mode GNU C (section 9), not a different dialect:
  it rejects the features that would need a runtime and otherwise compiles
  plain GNU C.

## 9. Kernel support policy

The kernel claim is a measured compatibility claim, not a safety claim.

- `-fcrug-kernel` is accepted by the driver and has no effect on code
  generation. It conflicts with `-fcrug-ownership`, `-fcrug-borrow`,
  `-fcrug-goroutines`, `-fcrug-channels` and `-fcrug-modern`, and those
  conflicts are hard errors (`docs/crug/FLAGS.md` section 2).
- Kernel code is plain GNU C in v1: no ownership analysis, no automatic drop,
  no borrow checking and no kernel `.crugffi` catalogue (ADR 0017, Proposed).
  The kernel qualification therefore says "this compiler builds and boots the
  pinned tree", not "kernel code is memory-safe".
- The pinned tree is Linux 6.18.57 (`docs/crug/KERNEL-BASELINE.md` section
  1). The primary evidence is the full x86-64 build (K5) and the boot smoke
  (K6); AArch64 support is a 15-object compile-only subset (K7), and no
  AArch64 boot is claimed.
- The release-candidate threshold is that K5 and K6 are green at the RC commit
  (`docs/crug/RELEASE-POLICY.md` section 4). Kernel codegen is compared
  object by object against the pinned compiler, with the one allowlisted
  timestamp object explained in `kernel/object-diff-allow.txt`.
- A future kernel ownership increment needs a superseding ADR naming the
  allocation and release pairs, the lock and RCU rules and the diagnostics; it
  must not change object bytes unless a program opts in.

## 10. Summary and amendments

What a user may rely on in v1:

- the pinned GCC baseline and the flag-off object identity;
- the frozen Crug-native ABI rows of version 1 and the runtime ABI version 1;
- `.crugffi` major-version rejection and unknown-clause rejection;
- append-only diagnostic codes, and the SARIF form that carries them as
  `ruleId` (`docs/crug/DIAGNOSTICS.md` section 2);
- the four supported hosts of section 7 and the kernel claim of section 9.

What is not promised in v1: language stability before 1.0, a shared-library
runtime ABI, symbol versioning, a language-version selector, kernel ownership
semantics, and the platforms and tooling listed as post-1.0 elsewhere.

| amendment | date | change | why |
|---|---|---|---|
| A-1 | 2026-10-20 | Initial ratification for Phase 23: language version 1 pre-1.0, Crug-native ABI 1, runtime ABI 1, PAL 1.1, `.crugffi` 1.0, the deprecation rules and the v1 host matrix. | The project needed one page that says what the gates are allowed to assume and how a promise may change. Everything here already had a canonical ADR or a measured check; this file only states the combination and the change procedure. |
| A-2 | 2026-10-26 | The PAL contract moves from 1.1 to 1.2: `crug_pal_thread_join` returns `int` so a backend can report a failed join. The language version 1 pre-1.0, Crug-native ABI 1, runtime ABI 1 and `.crugffi` 1.0 are unchanged. | A failed join was previously indistinguishable from a successful one; ADR 0011 A-2 gives the status and requires the handle to stay valid on failure. The four backends and the thirteen-case contract test moved together. |
