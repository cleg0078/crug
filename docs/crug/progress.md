# CRUG Project Status

Last updated: 2026-10-19

## Where the project stands

The repository contains specification text, a phase-gate adjudicator, a
reproducible GCC build, the `crugc` driver (Phase 1), and from Phases 4 and 5 the
first Crug productions - `own`, `move`, automatic destruction, `adopt`,
`release` and `drop` - with their analysis, diagnostics and dg suites, from
Phase 6 the borrow checker (`ref`, `mut`, `borrow`, `borrow_mut`) and from
Phase 7 last-use lifetimes and the depth-zero interprocedural policy. There
is no `libcrug` and no goroutine or channel support yet; the Phase 10 safe
types (`slice`, `option`, `result`, payload enums, owned payloads, `match`,
checked indexing) exist and are described below, Phase 11 lets the
optimizer use proven ownership (`-fcrug-alias-facts`, `mut`/`own` parameters
as `restrict`), and Phase 12 adds `c2crug`, a conservative C-to-Crug migration
tool (proof-based `own` insertion, `raw` for everything unproven). The `kernel-compat`
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

Phase 10 adds the safe core types. `slice<T>`, `option<T>`, `result<T,E>` and
payload `enum`s are ordinary records built by the C front end
(`gcc/c/crug-safe.cc`), with `slice (ptr, len)`, `some`/`none`/`ok`/`err` and
variant constructors, a `match` statement over the three sum shapes and checked
`s[i]` indexing (`__builtin_trap` on a length violation,
`-fno-crug-bounds-checks` to remove it). A payload enum is a record storage type
registered under its `enum` tag (tag `unsigned int` at offset 0, payload union
after it), so a tag reference keeps working while the value keeps a variant tag.
No runtime, helper or new GIMPLE opcode is involved, the constructors lower to
plain stores, and the layouts (`slice` 16/8; `option<int>` 8/4;
`result<int,int>` 8/4; `enum Message { Text (char *), Image (char *), Quit }`
16/8) are frozen in `docs/crug/ABI.md` section 3 and asserted by
`gcc/testsuite/crug.dg/safe-types/layout.c` and `payload-enum-layout.c`.
`make check-SAFE_TYPES` runs 77 expected passes,
`scripts/check-safe-types.sh` requires zero failures and
`scripts/check-safe-bounds.sh` asserts the trap and its absence.

An owned payload (increment 2b) is tracked by the ownership analysis, with
ownership derived from the type because `own` is erased from the declaration:
`Text (adopt (p, release))`, `Text (move (q))` and fresh allocations transfer
the destructor into the value, the drop at every exit is
`if (tag == V) { if (u.Text) release (u.Text); }` so an inactive variant never
reaches the call (asserted by `scripts/check-owned-payloads.sh` and the
`-O0` assembly it inspects), copying is `crug-UAM-003`, an owned scrutinee must
be moved into `match` (`crug-ADT-015`), an arm covering an owning variant must
bind the payload (`crug-ADT-020`), and the value may not cross a function
boundary by value (`crug-ADT-018`). `docs/crug/SAFE-TYPES.md` is the digest.
The Phase 10 gate (amendment A-13) has no prose left and **passes 10/10 at
`43996fe`** (logs under `docs/crug/gates/phase-10/43996fe/`), so M10 is
awarded: `make check-SAFE_TYPES`, the tally, the bounds script, the two layout
compiles, the forward-reference rejection, the non-exhaustive `match`
rejection, `scripts/check-owned-payloads.sh`, `make check-C_COMPAT` and
`scripts/check-diagnostics.sh`.

Phase 11 makes proven ownership visible to the optimizer. `-fcrug-alias-facts`
(off by default; implies `-fcrug-borrow`) compiles `mut` and `own` pointer
parameters as `restrict` and rejects a call site that cannot prove
exclusivity: a plain pointer at a `mut` parameter (`crug-BORROW-021`), a
shared borrow there (`crug-BORROW-022`), two borrows of one lender in one call
(`crug-BORROW-002`/`-003`) and a lender passed beside its own mutable borrow
(`crug-BORROW-023`). `-fcrug-dump=alias` records every derived fact, and
`scripts/check-stop-conditions.sh` audits each one against the source
declaration. The evidence is the three-arm matrix in `bench/`: each kernel is
built as plain C, C + hand-written `restrict`, and Crug `mut`/`ref`, with
one shared plain-C `main`, so the Crug and restrict kernel objects are
byte-identical at `-O2` and `-O3` (8/8 kernels); timing is reported but the
gate rests on identity because this host's runs of identical code swing by more
than a factor of two. `bench/run.py --matrix alias --check`, `--matrix alias
--lto` and `--plain-c --check` are the runnable checks, and
`docs/crug/OPTIMIZATION.md` is the digest. The Phase 11 gate (amendment A-14)
passes 7/7 at `135dea9` (logs under `docs/crug/gates/phase-11/135dea9/`),
so M11 is awarded.

Phase 12 adds `c2crug`, the migration tool (`./c2crug`, Python 3 standard
library only, decision record ADR 0023, digest `docs/crug/C2CRUG.md`). Safe mode
inserts `own` before a pointer only when a local structural proof holds -
known allocating origin (`malloc`/`calloc`/`realloc`/`reallocarray`/`strdup`/
`strndup`/`aligned_alloc` or a `.crugffi` `returns owned` function), exactly one
standalone release at the declaration's brace depth, no early exit that skips it,
and no address-of, reassignment, arithmetic, return, copy, unknown escape or use
after release - and then rewrites that release to `drop`. Everything else stays
byte-identical and is reported as `raw` with the line that defeated the proof.
Line count never changes, so comments and structure survive. The three modes are
`--analyze` (report only), `--safe -o OUT` (copy, refuse an existing OUT) and
`--diff` (print only); `--report` writes `crug-c2crug-report-v1`, validated by an
independent checker, and borrow suggestions (`ref` 0.80, `mut` 0.60) are advice
that no mode applies. The corpus `migration/` is eight runnable programs with
expected output, a `Makefile` test target and a libc sidecar: 5 `own` applied,
5 `raw` preserved, 10 of 222 source lines changed (4.5 percent), comment lines
16 -> 16, and both the original and the migrated tree print
`migration: 8 programs pass`. `scripts/check-c2crug.sh` runs the whole flow,
`scripts/test-c2crug.py` its 32 unit checks, `scripts/c2crug-diff.sh` the churn
band, and `make check-C2CRUG` both. The corpus replaces `corpus/` because those
thirty programs are compile-only fragments with no test suite (amendment A-15).
The Phase 12 gate (amendment A-15) passes 7/7 at `3aa78e7` (logs under
`docs/crug/gates/phase-12/3aa78e7/`), so M12 is awarded.

The ABI question now has a policy: ADR 0006 (Accepted 2026-10-19) fixes two ABI classes,
erasure rules, boundary moves and versioning, and creates the normative registry
`docs/crug/ABI.md`. The registry records the C-ABI-transparent guarantees that
already hold and freezes a Crug-native layout when the phase that ships it lands:
Phase 10 froze `slice`, `option`, `result` (increment 1) and the payload enum
(increment 2a), while the `chan` row stays pending until Phase 13. ADR 0006
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

The cost of the next GCC bump is measured, not estimated:
`docs/crug/GCC-UPGRADE.md` records that the fork touches 9 upstream files with
32 hunks, that 31 of those hunks still apply with identical context to
upstream master (2026-10-19), that all 23 attachment symbols and all 8 behavioural
pins hold, and that a 31-check acceptance matrix
(`scripts/verify-upgrade.sh`) passes 31/31 at the current commit.
`scripts/fork-delta.sh` and `scripts/check-gcc-drift.sh` keep the measurement
reproducible. ADR 0022 (Proposed) turns it into policy: a re-pin and a minor
upgrade are maintainer-executable on the matrix, a major upgrade needs a
superseding ADR and owner ratification.

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
| No stable diagnostic-code registry | docs/crug/DIAGNOSTICS.md (111 codes; 84 implemented through Phases 4 to 11) + scripts/check-diagnostics.sh |
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
| The safe core types did not exist, so "memory-safe Crug" had no data types and the Phase 10 layouts were still pending rows | `slice<T>`/`option<T>`/`result<T,E>`/payload enums/`match`/checked indexing in `gcc/c/crug-safe.cc`; 77-pass dg suite, `scripts/check-safe-types.sh`, `scripts/check-safe-bounds.sh` and `scripts/check-owned-payloads.sh`; `docs/crug/SAFE-TYPES.md`; frozen `ABI.md` rows; Phase 10 increments 1, 2a and 2b; phase-10 gate 10/10 |
| Ownership proved exclusivity but the optimizer never saw it, so `mut` was checked and then discarded | `-fcrug-alias-facts` (ADR 0010): `mut`/`own` parameters as `restrict`, BORROW-021..023 at unproven call sites, `-fcrug-dump=alias`, `bench/` three-arm matrix (Crug kernel objects byte-identical to the hand-written restrict twins, 8/8), `scripts/check-optimization.sh`, `scripts/check-stop-conditions.sh`; `docs/crug/OPTIMIZATION.md`; phase-11 gate 7/7 |
| Migrating C to Crug was unspecified, so "safe mode" had no proof rule and no corpus | `c2crug` (`--analyze`/`--safe`/`--diff`), ADR 0023, `docs/crug/C2CRUG.md`, `migration/` (8 programs with expected output), `docs/crug/c2crug-report.schema.json`, `scripts/validate-c2crug-report.py`, `scripts/c2crug-diff.sh`, `scripts/check-c2crug.sh`, `scripts/test-c2crug.py`; 5 own applied, 5 raw preserved, 4.5% churn, comments unchanged; phase-12 gate 7/7 |
| The cost of moving to a newer GCC was unmeasured, so the pin could not be revised on evidence | docs/crug/GCC-UPGRADE.md (9 files/32 hunks versus 7 new sources/5,021 lines; 31/32 hunk contexts clean on master, 23/23 symbols, 8/8 pins; 31-check matrix) + scripts/fork-delta.sh, scripts/check-gcc-drift.sh, scripts/verify-upgrade.sh + ADR 0022 (Proposed) |

## Still open

| ID | Item | Blocked on |
|----|------|------------|
| C-05 | P1 conformance refers to a release matrix that does not exist | RFC-0010 |
| -  | The Phase-19 per-object corpus list `kernel/KERNEL-CORPUS.md` does not exist; the kernel source is pinned but the compile corpus is not | Phase 19 gate |
| -  | Phase-20 golden corpus (50 error classes with stable wording) is not written | Crug frontend; docs/crug/DIAGNOSTICS.md seeds the classes |
| -  | `.crugffi` v1 deliberately excludes returned borrows, conditional ownership (`sqlite3_open`, `realloc`), refcounted APIs, struct members, macros, inline functions and variadic tails; such calls stay level 1 (`adopt` at the call site) | a later FFI revision (ADR 0007 D9) |
| -  | A sidecar for a header found through `-I` is discovered only when it sits next to the file containing the `#include`, because libcpp reports the include directive, not the includer directory | ADR 0007 amendment A-1; use `-fcrug-ffi=<path>` explicitly |
| -  | `slice`, `option`, `result` and payload enum layouts are frozen (`ABI.md` section 3), but the `chan` row is still pending, and no Crug-native type other than the four frozen rows may cross a C interface | Phases 13-14 (`chan`) |
| -  | Ownership transfer is checked only where a contract exists: a `.crugffi` sidecar makes a return value owned and a consuming parameter require `move`, but an unannotated C function still neither returns an owned value nor receives one, and an owned value passed to an `own` parameter is diagnosed only under `-fcrug-strict` (`crug-OWN-011`) | a later phase (call-site direction); recorded in docs/crug/DROP.md section 8 and docs/crug/FFI.md |
| -  | Borrow checking stops at whole objects: struct and array-element borrows, mutable reborrow, and interior mutability are not implemented. A plain pointer at a `ref` parameter is still accepted, and at a `mut` parameter it is accepted only without `-fcrug-alias-facts` (with the flag it is `crug-BORROW-021`) | a later phase (place-level borrows); the FFI half of the call-site direction was decided in Phase 9; recorded in docs/crug/BORROW.md sections 2.4 and 5 |
| -  | `chan T` does not parse; an owned payload is tracked only for local values and `match` bindings, so an owning payload enum stored in a struct, union or array, or held in an `option`/`result` payload, has no tracked destructor | Phase 13-14 (`chan`); a later ownership increment for aggregates |
| -  | The version macros `__CRUG_VERSION_*`, `__CRUG_RUNTIME_ABI__` and `__CRUG_FFI_VERSION__` asked for by RFC-0001 section 10 are not defined | ADR 0013 (versioning); the feature macros exist today |
| -  | The alias fact is local to a callee's parameters: an unannotated caller is not checked, and no fact is derived for constructs whose ownership is untracked (owning payload enums inside aggregates, owned payloads in `option`/`result`) | ADR 0010 D8; a later ownership increment for aggregates |
| -  | ADR 0022 (baseline upgrade policy) is Proposed, so the P/M/J classes, the acceptance rule and the ratification split are not yet binding | owner ratification |
| -  | The upgrade procedure has never been executed against a real new release; the first M- or J-class upgrade is the drill | first GCC release after 16.2 |

## Next actions, in dependency order

1. **Push and open Phase 13** (the platform abstraction layer): the Phase 12
   gate passes 7/7 at the gate commit; push the branch, then write the PAL v1
   contract ADR (required file name `0011-pal-contract`) and build
   `libcrug/core` against the fake PAL. Phase 13's gate also needs
   `scripts/lint-no-os-apis.sh`, which does not exist yet.
2. **Ratify ADR 0021, ADR 0022 and ADR 0019/0020** as the owner: the strict
   policy, the baseline upgrade policy and the two borrow policies are implemented
   and measured but still carry Status Proposed.
3. **Write the Phase-19 kernel compile corpus** (`kernel/KERNEL-CORPUS.md`) on top
   of the pinned Linux 6.18.57 tree. `-fcrug-kernel` currently rejects
   `-fcrug-ownership` and `-fcrug-borrow` until that phase decides kernel drop
   lowering.

## Working rules

- PRECEDENCE.md section 2 decides any document conflict.
- A semantic change needs an ADR plus tests in `gcc/testsuite/crug.dg/`.
- No phase gate is skipped; EXIT-GATES.md is the definition of "done", and a claim
  without an archived `scripts/gate.sh` run does not count.
