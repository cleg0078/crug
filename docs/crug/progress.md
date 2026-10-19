# CRUG Project Status

Last updated: 2026-10-19

## Where the project stands

The repository contains specification text, a phase-gate adjudicator, a
reproducible GCC build, the `crugc` driver (Phase 1), and from Phases 4 and 5 the
first Crug productions - `own`, `move`, automatic destruction, `adopt`,
`release` and `drop` - with their analysis, diagnostics and dg suites, from
Phase 6 the borrow checker (`ref`, `mut`, `borrow`, `borrow_mut`) and from
Phase 7 last-use lifetimes and the depth-zero interprocedural policy. There
is no `libcrug` and no goroutine, channel or `slice`/`match` support yet. The `kernel-compat`
CI job builds the pinned baseline on every push and is a required check on `main`.
Four specification
documents exist (10,513 lines): the RFC, the master specification, the Rust memory
gaps analysis, and the production roadmap. An analysis of those documents produced
eight findings; this file tracks what has been resolved and what blocks progress.

The RFC has since been revised to 0.2-draft: its surface grammar is now normative (sections
13-25) and it adopts the ADR 0003/0004/0005 ownership and drop rules. Six decision records
(0001, 0002, 0003, 0004, 0005, 0018) and the precedence register were ratified on
2026-10-18, and the repository is now GPL-3.0-or-later. The master specification, roadmap
and gaps analysis still carry their original wording.

Reusable infrastructure now exists: `scripts/gate.sh` adjudicates every phase gate
and archives evidence, and the documentation set below gives the specification a
single authority order, a grammar, and decision records for the open questions.
The pinned GCC 16.2.0 tree is imported into git and tagged `releases/gcc-16.2`,
so the repository root is the GCC top level and the fork base is reproducible
from a clone rather than from a tarball on one machine. The C testsuite runs
through a root-free DejaGnu (`scripts/bootstrap-dejagnu.sh`), and its 224,481-pass
result is archived at `docs/crug/baseline/phase0/`; the phase-0 and phase-1 gates
pass with archived checks, and `docs/crug/EXIT-GATES.md` was ratified on 2026-10-19,
so the gate definitions are binding and the phase-to-milestone mapping (C-06) is
settled. `gcc/crugc` is a launcher around the built GCC driver
for now, so plain C compatibility is measured rather than assumed
(`make check-C_COMPAT`, `scripts/diff-cc.sh`). Phase 2 is closed:
`-fcrug-kernel` is accepted and ignored, the 21 roadmap work items run against a
named kernel-style corpus (`make check-KERNEL_COMPAT`), object scans show no
`__crug_` symbol and no `libcrug` dependency, and freestanding units carry no
hidden runtime (`scripts/check-kernel-freestanding.sh`). The CI half runs:
`.github/workflows/kernel-compat.yml` builds the pinned baseline on every push,
and branch protection on `main` requires the `kernel-compat` check, so Phase 2
and M2 closed on 2026-10-19 at commit `78c759b`. The repository is public since
2026-10-19, which is what makes free branch protection and the faster public
runner available.

Phase 3 is implemented: the ten `-fcrug-*` options are declared and closed over
their implications, the feature macros exist only in Crug mode, and the flag
machinery changes no parse and no object byte. `docs/crug/FLAGS.md` is the
digest; `scripts/check-crug-flags.sh` (149 work items) and
`scripts/check-gnu-c-compat.sh` (50 files) are the local checks. The
productions the flags will gate - `own`, `move`, `borrow`, `slice`, `chan`,
`match` - do not exist yet: that is Phase 4 onward, and the contextual words
still parse as ordinary C identifiers under every combination. The Phase 3 gate
passes its four checks at commit `78c759b` (evidence under
`docs/crug/gates/phase-3/78c759b/`), and its prerequisite - the `kernel-compat`
job being mandatory at HEAD - is met, so M3 closed on 2026-10-19.

Phase 4 implements the ownership MVP: `own` is a declaration specifier that is
erased from the type, `move` is an intrinsic identifier that lowers to a
register copy, and the analysis tracks UNINIT/OWNED/MOVED/MAYBE_MOVED at
variable granularity. It rejects use-after-move, double move, implicit copy of
an owned value and use of a possibly-moved value after a join
(`crug-UAM-001`..`crug-UAM-004`), and `-fcrug-dump=ownership` prints the join
state. The ABI obligation is measured, not asserted: `abi-ptr.c` keeps
`sizeof`/`_Alignof` identical and `move-zero-cost.c` disassembles to zero calls.
`docs/crug/OWNERSHIP.md` is the digest; `make check-OWNERSHIP` runs 17 expected
passes under `gcc/testsuite/crug.dg/ownership/`.

Phase 5 adds automatic destruction: destructors are analysis metadata (ADR 0005
A-2), they come from `adopt (p, d)` or from the allocation catalogue
(`malloc`/`calloc`/`realloc`/`strdup`/`aligned_alloc` are freed with `free` when
it is declared), and every owning object is destroyed exactly once on scope
exit, `return`, `break`, `continue` and `goto`-out, in reverse declaration order,
including owning parameters (ADR 0005 A-1). `goto`-into-scope, ambiguous joins,
conditional initialization, assignment over an owned value and non-owned `drop`
operands are rejected with stable codes; `setjmp` and never-adopted values warn.
`docs/crug/DROP.md` is the digest; `make check-DROP` runs 39 expected passes and
the six `scripts/check-drop-*.sh` checks assert exactly-once counts, sanitizer
cleanliness, absence of `__crug_` helper calls and the dump. The Phase 5 gate
passes 7/7 at `eeac7df` (logs under `docs/crug/gates/phase-5/eeac7df/`), and the
Phase 4 gate was re-adjudicated at the same commit with 7/7.

Phase 6 adds the lexical borrow checker: `ref` and `mut` are qualifiers on
pointer declarators, `borrow`/`borrow_mut` are intrinsic identifiers that lower
to plain pointers, and one shared with many `ref` reads or exactly one `mut` is
enforced for the lexical extent of the borrow. A borrow may name a whole owned
object or an existing shared borrow; members, array elements, dereferences and
plain pointers are refused (`crug-BORROW-009`), as are a mutable borrow of a
borrow or of a non-owned object (`crug-BORROW-010`) and a mutable borrow
initialized from a shared one (`crug-BORROW-012`). Moves, drops, assignments and
reads that conflict with a live borrow are rejected with `crug-BORROW-002`..`-005`,
`-008` and `-014`..`-016`; an inner borrow cannot escape its block
(`crug-BORROW-007`). `ref`/`mut` change no `sizeof`, `_Alignof` or object byte:
`scripts/check-borrow-cost.sh` compares a borrowed function with its
plain-pointer twin and `scripts/check-borrow-abi.sh` asserts the layout
identities. `docs/crug/BORROW.md` is the digest; `make check-BORROW` runs 62
expected passes under `gcc/testsuite/crug.dg/borrow/`. The Phase 6 gate passes
7/7 at `848b02e` (logs under `docs/crug/gates/phase-6/848b02e/`).

Phase 7 turns the lexical borrow checker into a liveness analysis (ADR 0020): a
borrow ends after the last statement that reads it, so the owner can then be
moved, dropped, released or mutably borrowed. The set of live borrows is
computed backward over the structured body (`in(S) = use(S) | (out(S) -
def(S))`), joins union, loops are conservative and a `goto` in the function
falls back to the lexical rule. The interprocedural policy is depth zero (ADR
0019): a borrow crosses a call only as an argument to a `ref`/`mut` parameter
(`crug-BORROW-020`), may not be returned (`crug-BORROW-019`) or stored in
memory or a static (`crug-BORROW-018`), and no function is summarized, so a call
can never extend a borrow. `make check-BORROW` runs 88 expected passes over 46
fixtures, six programs under `real-c/` are compiled and run, and
`-fcrug-dump=borrow` marks a last-use end with `(last-use)`.
`docs/crug/BORROW.md` is the digest, and the Phase 7 gate passes 7/7 at
`f6fbde4` (logs under `docs/crug/gates/phase-7/f6fbde4/`).

Phase 8 adds the escape hatches and the strict policy (ADR 0021). `raw` is a
pointer with no ownership and the plain pointer's representation; `unsafe { ... }`
is a scope that suppresses exactly the unanalysed operations and nothing else.
`-fcrug-strict` rejects, outside an `unsafe` block, a `raw` dereference
(`crug-SAFE-001`), an integer-pointer conversion (`-002`), an owned value cast to
an unmanaged pointer (`-003`), an `asm` statement (`-004`) and the
`setjmp`/`longjmp` family (`-005`); at a call it requires `move` for an `own`
parameter (`crug-OWN-011`) and `release` or an `unsafe` block for a callee with
no ownership contract (`crug-OWN-012`), and a discarded `adopt` is an error in
both modes (`crug-OWN-013`). Strict mode changes no object byte:
`scripts/check-strict-codegen.sh` compares four programs compiled with and
without the flag. `docs/crug/STRICT.md` is the digest and `make check-STRICT_UNSAFE`
runs 21 expected passes, and the Phase 8 gate passes 7/7 at `e3d8662` (logs
under `docs/crug/gates/phase-8/e3d8662/`; the definition is the seven-check list
in `docs/crug/EXIT-GATES.md`, amendment A-9).

Phase 9 adds the production C FFI layer (ADR 0007). `.crugffi` sidecars are
parsed by `gcc/c-family/crug-ffi.cc` and consumed by the ownership pass:
`-fcrug-ffi=<file>` names a contract explicitly (repeatable, merged in
command-line order), `-fcrug-ffi-discover` finds one next to a quoted include,
and both imply `-fcrug-ownership` and define `__CRUG_FFI_VERSION__` as 100. A
`returns owned` function hands the caller an owned value with the contract's
destructor, `consumes` requires `move` (`crug-FFI-007`), `out_owned` fills an
owned variable (`-008`), borrows are checked against the live borrow set, and a
callback argument must be a function (`-009`). Splitting a printf-like format
argument is deliberately absent: a variadic tail is not annotated. Metadata
never changes generated code: `scripts/check-ffi-codegen.sh` compares objects
with and without a sidecar and `scripts/check-ffi-integration.sh` hashes
`libc.so.6`, `libsqlite3.so.0`, `libcurl.so.4` and `libssl.so.3` before and
after building four programs that open and close a real handle. The suite is 19
cases (`make check-FFI`), `docs/crug/FFI.md` is the digest, and the Phase 9 gate
is the six-check list in `docs/crug/EXIT-GATES.md` (amendment A-10). The Phase 9
gate passes 6/6 at `e1052f9` (logs under `docs/crug/gates/phase-9/e1052f9/`).

The ABI question now has a policy: ADR 0006 (Accepted 2026-10-19) fixes two ABI classes,
erasure rules, boundary moves and versioning, and creates the normative registry
`docs/crug/ABI.md`. The registry records the C-ABI-transparent guarantees that
already hold and leaves every Crug-native layout (`slice`, `option`, `result`,
payload enum, `chan`) pending until the phase that ships it freezes it. ADR 0006
was ratified on 2026-10-19, so the policy is binding; only the Crug-native layout rows remain pending.

The FFI metadata question is now answered and binding: ADR 0007 (Accepted 2026-10-19) defines the
`.crugffi` v1 format - a versioned, line-oriented sidecar that states ownership,
borrowing, destructors, out-parameters, nullability, callbacks, blocking and
thread affinity for C functions, and nothing about layout. It cannot change
codegen, is never loaded at run time, rejects conflicts deterministically, and
under `-fcrug-strict` must be named explicitly rather than discovered. A reference
fixture, `gcc/testsuite/crug.dg/ffi/libc.crugffi`, implements the small libc
catalog of MASTER_SPEC section 16.5 minus `realloc`. Phase 9 implemented the parser, the discovery option and the call-site
consumption described in the paragraph above.

## Resolved in this revision

| Item | Artefact |
|------|----------|
| Documents had no order of authority | spec/000-PRECEDENCE.md (authority order + conflict register C-01..C-11) |
| Context-sensitive words had no disambiguation rule | docs/crug/adr/0002-contextual-keywords.md |
| Aggregate ownership was undefined (double-drop hole) | docs/crug/adr/0004-ownership-analysis-layer.md |
| Ownership storage could have required hidden state | docs/crug/adr/0003-ownership-metadata-storage.md (static metadata, ABI erasure) |
| Destruction across goto/setjmp/exit/tasks was undefined | docs/crug/adr/0005-drop-lowering.md |
| Syntax was sketched but ambiguous | docs/crug/adr/0018-grammar.md |
| RFC-0001 stated syntax without deciding ambiguity, attachment or ABI erasure | spec/CRUG_RFC_0001.md 0.2-draft sections 13-25 + Appendix H |
| ADR numbering conflicted with MASTER_SPEC §79 | resolved 2026-10-18: ADR 0003/0004 split + ADR 0005 renamed; mapping in docs/crug/adr/README.md |
| The decision set was unratified, so nothing could be cited as settled | ten ADRs carry Status Accepted (six on 2026-10-18; ADRs 0006 and 0007 on 2026-10-19; ADRs 0019 and 0020 on 2026-10-19); spec/000-PRECEDENCE.md Accepted (2026-10-18) |
| No kernel source was pinned, so K0 and Phase 19 were untestable | docs/crug/KERNEL-BASELINE.md (Linux 6.18.57 LTS, tag/commit/SHA256, x86_64_defconfig, qemu markers) + scripts/kernel-pin.sh; C-09 resolved 2026-10-18 |
| Exit gates were partially unfalsifiable | docs/crug/EXIT-GATES.md |
| No stable diagnostic-code registry | docs/crug/DIAGNOSTICS.md (98 codes; 62 implemented through Phases 4 to 9) + scripts/check-diagnostics.sh |
| The kernel baseline had no CI protection, so Phase 2 could not close and Phase 4 could not start | `.github/workflows/kernel-compat.yml` green on `78c759b`; branch protection on `main` requires `kernel-compat`; phase-2 and phase-3 gates 4/4 archived under docs/crug/gates/phase-{2,3}/78c759b/ |
| No mechanism existed to evaluate a gate | scripts/gate.sh (`--list`, `--run`, archives per-check logs) |
| No upstream GCC was pinned | docs/crug/GCC-BASELINE.md + docs/crug/adr/0001-gcc-16.2-baseline.md + docs/crug/gcc-baseline.sha512 |
| The fork base lived outside git, so the tree could not be reproduced from a clone | GCC 16.2.0 imported as commit 57ccbae and tagged `releases/gcc-16.2`; `git diff releases/gcc-16.2..HEAD -- gcc/` is the fork delta |
| No way to reproduce the toolchain | Makefile (`make baseline`: verify, prerequisites, configure, all-gcc; `make build-full` adds target libraries and links; `make help`) |
| Repository licence was MIT, incompatible with the GCC fork | LICENSE (GPL-3.0-or-later), NOTICE (per-path map); C-07 resolved by the owner on 2026-10-18 |
| Line citations rotted as documents were revised | every citation now names a section or a gate phase and check (rule in spec/000-PRECEDENCE.md section 2) |
| The C testsuite could not run (no root, no DejaGnu) | `make bootstrap-dejagnu` installs DejaGnu/Expect/Tcl under `tools/`; `make check-c` runs the suite; result archived at docs/crug/baseline/phase0/ (224,481 pass, 84 environmental fail) |
| Phase 0 was unadjudicated | `scripts/gate.sh --run phase-0` passed 5/5 checks; logs under docs/crug/gates/phase-0/ |
| No driver existed, so plain-C compatibility could not be measured | `gcc/crugc` launcher (docs/crug/DRIVER.md) + `corpus/` (30 programs) + `scripts/diff-cc.sh` + `make check-C_COMPAT`; phase-1 gate archived under docs/crug/gates/phase-1/ |
| Exit gates were Proposed, so no gate definition was binding, and the phase-to-milestone mapping (C-06) was unsettled | docs/crug/EXIT-GATES.md Status Accepted (2026-10-19); C-06 settled by its section 7; amendment record in section 12 |
| There was no kernel-mode flag, corpus or local runner, so the kernel constraint was untested | `-fcrug-kernel` in `gcc/common.opt` (Common Ignore); 18-source corpus under `gcc/testsuite/crug.dg/kernel/`; `scripts/test-kernel-compat.sh` (21/21), `scripts/check-kernel-objects.sh`, `scripts/check-kernel-freestanding.sh`; `make check-KERNEL_COMPAT`; phase-2 gate archived for checks 1-3 |
| Nothing would have protected the kernel baseline in CI | `.github/workflows/kernel-compat.yml` (builds the pinned baseline, runs the suite, 300-minute cap, frees disk first) green on `78c759b` + `scripts/check-ci-gate.sh` (asserts the check concluded success and is a required status check) |
| No C ABI policy existed, so qualifier erasure, Crug-native type layouts and boundary moves were unsettled | docs/crug/adr/0006-c-abi-policy.md (two ABI classes, erasure, boundary moves, targets, versioning, test obligations) |
| No single place recorded ABI facts, while the Phase 10 gate already required one | docs/crug/ABI.md: C-ABI-transparent guarantees recorded, Crug-native rows pending, registry rules and layout-assertion obligation |
| The C FFI sidecar was illustrative only, so Level 2 ownership had no decidable format | docs/crug/adr/0007-ffi-metadata-format.md (v1 grammar, clause semantics, discovery, versioning, trust model) + reference fixture gcc/testsuite/crug.dg/ffi/libc.crugffi |
| C FFI contracts were a document, not a compiler feature | `gcc/c-family/crug-ffi.cc` (v1 parser, discovery callback, conflict detection), consumption in `gcc/c/crug-ownership.cc`, `-fcrug-ffi=` and `-fcrug-ffi-discover`; 19-case suite, four library integrations, five `scripts/check-ffi-*.sh`; docs/crug/FFI.md; phase-9 gate |
| The Phase 9 gate invoked the sidecar flag in a form the format does not define | docs/crug/EXIT-GATES.md phase 9 checks 4-5 now use `-fcrug-ffi=<path>` (amendment A-3) |
| No feature flag existed, so no later phase could be gated on one and no build could say which Crug features it wanted | ten `-fcrug-*` options in `gcc/common.opt` with closure in `gcc/c-family/crug-common.cc`; `docs/crug/FLAGS.md`; `scripts/check-crug-flags.sh` (149 work items), `scripts/check-gnu-c-compat.sh` (50 files), `scripts/diff-obj.sh`; fixtures in `gcc/testsuite/crug.dg/flags/` |
| Predefined feature macros contradicted the flags-off identity guarantee | ADR 0018 D9 R6 amendment A-1 and spec/000-PRECEDENCE.md C-12: `__CRUG__` and the feature macros exist only when a `-fcrug-*` option is active |
| Ownership had no implementation, so the state machine and the zero-metadata claim were untested | `own`/`move` front-end (gcc/c/crug-ownership.cc), 17-pass dg suite, `-fcrug-dump=ownership`; docs/crug/OWNERSHIP.md; phase-4 gate 7/7 |
| Destruction was undefined, so every owning object leaked and the O1 claim was untestable | drop lowering in `gcc/c/crug-ownership.cc`, `adopt`/`release`/`drop` intrinsics, 39-pass dg suite, six `scripts/check-drop-*.sh` checks, `-fcrug-dump=drop`; docs/crug/DROP.md; ADR 0005 A-1..A-7; phase-5 gate |
| Borrowing did not exist, so `ref`/`mut` were unimplemented words and the B1 class was unclaimed | `ref`/`mut` qualifiers and `borrow`/`borrow_mut` intrinsics in `gcc/c/crug-ownership.cc`; 62-pass dg suite; five `scripts/check-borrow-*.sh` checks; `-fcrug-dump=borrow`; docs/crug/BORROW.md; ADR 0018 A-4/A-5; phase-6 gate |
| Unanalysed operations (deref of a plain view, casts, asm, setjmp) had no policy, so "strict mode" was a word without a definition | ADR 0021 (Accepted 2026-10-19) + docs/crug/STRICT.md: `raw`, `unsafe` and five `crug-SAFE-*` restrictions with `-fcrug-strict`; C-08 resolved |
| A borrow lived for its whole block and could cross calls unchecked | last-use liveness in `gcc/c/crug-ownership.cc` (ADR 0020), depth-zero call policy (ADR 0019) with `crug-BORROW-018`..`-020`, 88-pass dg suite over 46 fixtures, six `real-c` programs, three new `scripts/check-borrow-*.sh` checks, `(last-use)` in the dump; docs/crug/BORROW.md; phase-7 gate |

## Still open

| ID | Item | Blocked on |
|----|------|------------|
| C-05 | P1 conformance refers to a release matrix that does not exist | RFC-0010 |
| -  | The Phase-19 per-object corpus list `kernel/KERNEL-CORPUS.md` does not exist; the kernel source is pinned but the compile corpus is not | Phase 19 gate |
| -  | Phase-20 golden corpus (50 error classes with stable wording) is not written | Crug frontend; docs/crug/DIAGNOSTICS.md seeds the classes |
| -  | `.crugffi` v1 deliberately excludes returned borrows, conditional ownership (`sqlite3_open`, `realloc`), refcounted APIs, struct members, macros, inline functions and variadic tails; such calls stay level 1 (`adopt` at the call site) | a later FFI revision (ADR 0007 D9) |
| -  | A sidecar for a header found through `-I` is discovered only when it sits next to the file containing the `#include`, because libcpp reports the include directive, not the includer directory | ADR 0007 amendment A-1; use `-fcrug-ffi=<path>` explicitly |
| -  | Every Crug-native layout is a pending row in `docs/crug/ABI.md`; no size, alignment or calling representation is frozen | Phases 10 and 13-14 (slice/option/payload enum, then chan); `result` also needs its focused RFC |
| -  | Ownership transfer is checked only where a contract exists: a `.crugffi` sidecar makes a return value owned and a consuming parameter require `move`, but an unannotated C function still neither returns an owned value nor receives one, and an owned value passed to an `own` parameter is diagnosed only under `-fcrug-strict` (`crug-OWN-011`) | a later phase (call-site direction); recorded in docs/crug/DROP.md section 8 and docs/crug/FFI.md |
| -  | Borrow checking stops at whole objects: struct and array-element borrows, mutable reborrow, and interior mutability are not implemented, and a plain pointer may still be passed where a `ref`/`mut` parameter is expected (probed: `read_ref (plain)` compiles) | Phase 12 (place-level borrows); the FFI half of the call-site direction was decided in Phase 9 (a plain pointer still has no borrow contract, so `ref`/`mut` parameters remain stricter than `.crugffi`); recorded in docs/crug/BORROW.md section 5 |
| -  | `slice<T>`, `chan T`, payload enums and `match` still do not parse | Phases 10 and 13-14 |
| -  | The version macros `__CRUG_VERSION_*`, `__CRUG_RUNTIME_ABI__` and `__CRUG_FFI_VERSION__` asked for by RFC-0001 section 10 are not defined | ADR 0013 (versioning); the feature macros exist today |

## Next actions, in dependency order

1. **Start Phase 10** (safe core types): `slice<T>`, `option<T>`, `result<T,E>`,
   payload enums and `match`, with the layouts this phase freezes recorded in
   `docs/crug/ABI.md` and active-variant destruction for ADTs.
2. **Ratify ADR 0021 and ADR 0019/0020** as the owner: the strict policy and the
   two borrow policies are implemented and tested but still carry Status Proposed.
3. **Write the Phase-19 kernel compile corpus** (`kernel/KERNEL-CORPUS.md`) on top
   of the pinned Linux 6.18.57 tree. `-fcrug-kernel` currently rejects
   `-fcrug-ownership` and `-fcrug-borrow` until that phase decides kernel drop
   lowering.
   lowering.

## Working rules

- PRECEDENCE.md section 2 decides any document conflict.
- A semantic change needs an ADR plus tests in `gcc/testsuite/crug.dg/`.
- No phase gate is skipped; EXIT-GATES.md is the definition of "done", and a claim
  without an archived `scripts/gate.sh` run does not count.
