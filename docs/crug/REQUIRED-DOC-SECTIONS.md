# Required document sections

Status: Phase 23 contract, 2026-10-20. This file is the manifest the
documentation gate reads: `scripts/check-docs.sh` checks every row here and
the Phase 23 gate runs that script. A document is compliant when it exists,
opens with a title, states its status and contains every listed section.

## 1. The rule

The documentation set is a contract with the reader, not a pile of notes. A
document that exists but loses a section breaks the same way an unchecked
feature does, so the manifest names, per document, the sections that must stay.
The checker is deliberately mechanical: exact headings, a status line, and no
unqualified claim that a post-1.0 feature is present.

The manifest covers every top-level page of `docs/crug/`, this one included: the checker also fails when a page exists without a row, so a new document cannot be added without naming its required sections. It covers only the top-level pages of `docs/crug/`. ADRs under
`docs/crug/adr/`, gate archives under `docs/crug/gates/` and the RFC text are
governed by their own rules (`docs/crug/adr/README.md`,
`docs/crug/EXIT-GATES.md` section 1).

## 2. Common requirements

Every document in the manifest must satisfy all of these, regardless of its
section list.

- **Title.** The first non-empty line is a level-one heading (`# `).
- **Status.** One of the first fifteen lines contains `Status:`, so a reader
  can tell an implemented page from a proposed one without reading the whole
  file. A living page may write its status in prose as long as the token is
  there.
- **Sections.** Every `## ` heading listed in the manifest appears verbatim
  (leading and trailing whitespace collapsed). Additional sections are
  allowed; removing or renaming a listed one needs a manifest change.
- **No unqualified post-1.0 claim.** The rule in section 4.
- **Cross-references.** A page names the canonical ADR or the phase gate that
  proves its claims, by relative path, so a reader can follow the evidence.

## 3. The manifest

Each row is a document and the `## ` headings it must contain, separated by
semicolons. Rows are parsed by `scripts/check-docs.sh`; keep the format.

| document | required sections |
|---|---|
| docs/crug/REQUIRED-DOC-SECTIONS.md | ## 1. The rule ; ## 2. Common requirements ; ## 3. The manifest ; ## 4. The post-1.0 claim rule ; ## 5. Changing the manifest |
| docs/crug/ABI.md | ## 1. How a row gets frozen ; ## 6. Versioning |
| docs/crug/BORROW.md | ## 1. What the checker implements ; ## 2. The rules ; ## 5. What Phase 6 does not do |
| docs/crug/BORROWING.md | ## 1. What a borrow is ; ## 2. The rules ; ## 3. Lifetimes and last use ; ## 4. Borrows across calls ; ## 5. Diagnostics ; ## 6. Limits |
| docs/crug/BUILD-INSTRUCTIONS.md | ## 1. Prerequisites ; ## 2. Build from source ; ## 3. Build the release artefacts ; ## 4. Build and use the container image ; ## 5. Install tree layout ; ## 6. Verify ; ## 7. Supported hosts |
| docs/crug/C2CRUG.md | ## 1. Modes ; ## 3. The proof ; ## 7. Limits |
| docs/crug/CHANNELS.md | ## 1. The surface ; ## 4. Runtime API ; ## 5. Limits |
| docs/crug/COMPATIBILITY-POLICY.md | ## 1. Language versioning ; ## 2. Crug ABI versioning ; ## 3. .crugffi versioning ; ## 4. libcrug ABI policy ; ## 5. Feature deprecation ; ## 6. GCC baseline policy ; ## 7. Supported platforms ; ## 8. Supported C dialects ; ## 9. Kernel support policy ; ## 10. Summary and amendments |
| docs/crug/COMPILER-INTERNALS.md | ## 1. Repository layout ; ## 2. The fork delta ; ## 3. Attachment points ; ## 4. Passes and lowering ; ## 5. Adding a feature ; ## 6. Upgrading GCC |
| docs/crug/CONCURRENCY.md | ## 1. The concurrency surface ; ## 2. Tasks and scheduling ; ## 3. Scopes ; ## 4. Channels and select ; ## 5. Send and Sync ; ## 6. Runtime and ports ; ## 7. Limits ; ## 8. Tests |
| docs/crug/C-FFI.md | ## 1. The contract format ; ## 2. Ownership at the boundary ; ## 3. Thread affinity, Send and Sync ; ## 4. Discovery ; ## 5. Diagnostics ; ## 6. Limits ; ## 7. Tests |
| docs/crug/DIAGNOSTICS.md | ## 2. Format and stability rules ; ## 4. Registry ; ## 9. Common errors and how to fix them |
| docs/crug/DRIVER.md | ## What exists ; ## What it does not do |
| docs/crug/DROP.md | ## 1. What this phase implements ; ## 4. Where destruction happens |
| docs/crug/EXIT-GATES.md | ## 2. How a gate is adjudicated ; ## 7. Milestone resolution |
| docs/crug/FFI.md | ## 1. What a contract says ; ## 4. Diagnostics ; ## 5. What Phase 9 does not do |
| docs/crug/FLAGS.md | ## 1. The option set ; ## 3. Feature macros ; ## 4. What is guaranteed |
| docs/crug/FREESTANDING.md | ## 1. What freestanding means here ; ## 2. Kernel compatibility mode ; ## 3. What the compiler avoids ; ## 4. Supported targets ; ## 5. Limits ; ## 6. Tests |
| docs/crug/FUZZING.md | ## 1. What is fuzzed, and what is not ; ## 4. Running the checks |
| docs/crug/GCC-BASELINE.md | ## 1. Compiler baseline ; ## 7. Upgrading the baseline |
| docs/crug/GCC-UPGRADE.md | ## 1. Short answer ; ## 4. Upgrade procedure ; ## 5. Acceptance matrix |
| docs/crug/GOROUTINES.md | ## 1. Surface ; ## 8. Tests |
| docs/crug/GRAMMAR.md | ## 1. Lexical rules and contextual keywords ; ## 2. Declarations and qualifiers ; ## 3. Statements ; ## 4. Types, payload enums and match ; ## 5. Where the grammar is defined ; ## 6. Tests |
| docs/crug/KERNEL-BASELINE.md | ## 1. Pinned artefact ; ## 8. Evidence |
| docs/crug/KERNEL-COMPATIBILITY.md | ## 1. What Phase 2 claims ; ## 8. Known limits |
| docs/crug/KNOWN-ISSUES.md | ## 1. Format ; ## 2. Ledger |
| docs/crug/LANGUAGE.md | ## 1. What the language adds ; ## 2. Plain C stays plain C ; ## 3. Feature gates and dialect selection ; ## 4. What is not in the language ; ## 5. Canonical references |
| docs/crug/MIGRATION-EXAMPLES.md | ## 1. How the executable blocks are marked ; ## 8. What the examples do not show |
| docs/crug/MIGRATION.md | ## 1. The c2crug tool ; ## 2. Modes ; ## 3. What it proves ; ## 4. The report ; ## 5. The corpus ; ## 6. Worked examples ; ## 7. Limits |
| docs/crug/OPTIMIZATION.md | ## 1. Turning it on ; ## 7. Limits |
| docs/crug/OWNERSHIP.md | ## 1. What is accepted ; ## 3. Diagnostics ; ## 5. What is deliberately absent |
| docs/crug/PAL.md | ## 1. The boundary ; ## 2. PAL surface (v1.2) ; ## 7. Versioning |
| docs/crug/PERFORMANCE.md | ## 1. Method and hardware ; ## 7. What this does not claim |
| docs/crug/PORTABILITY.md | ## 1. Two axes, one core ; ## 2. The v1 matrix ; ## 8. Where platform code may live |
| docs/crug/progress.md | ## Where the project stands ; ## Still open ; ## Next actions, in dependency order |
| docs/crug/RELEASE-NOTES.md | ## 1. What this candidate is ; ## 2. Highlights ; ## 3. Qualification evidence ; ## 4. Install and verify ; ## 5. Known limitations ; ## 6. Compatibility and support ; ## 7. What is not in this candidate |
| docs/crug/RELEASE-POLICY.md | ## 1. How a threshold is ratified ; ## 6. Amendments |
| docs/crug/SAFE-TYPES.md | ## 1. What is implemented ; ## 2. Layouts (frozen, x86_64-pc-linux-gnu) ; ## 8. Checked indexing |
| docs/crug/SCOPE.md | ## 1. The statement ; ## 5. Limits |
| docs/crug/SEND-SYNC.md | ## 1. When the checks are active ; ## 8. Limits |
| docs/crug/STRICT.md | ## 1. What strict mode is ; ## 5. What strict mode does not do |
| docs/crug/TASKS.md | ## 1. The model ; ## 6. Limits, stated not discovered |
| docs/crug/TESTING.md | ## 1. The test layers ; ## 2. Running the suites ; ## 3. Golden corpora ; ## 4. The gate adjudicator ; ## 5. Continuous integration ; ## 6. Adding a test |
| docs/crug/TOOLING.md | ## 1. Compiler options and feature gates ; ## 2. Machine-readable diagnostics ; ## 5. Formatter |
| docs/crug/UNSAFE.md | ## 1. Strict mode ; ## 2. `raw` pointers ; ## 3. `unsafe` blocks ; ## 4. Ownership at boundaries ; ## 5. Diagnostics ; ## 6. What unsafe does not make safe ; ## 7. Tests |

## 4. The post-1.0 claim rule

The roadmap's post-1.0 list (`spec/CRUG_PRODUCTION_ROADMAP.md` section 32)
must not be described as present. A document may discuss any of those items,
but the paragraph doing so must also say, in the same paragraph, that the item
is absent or later: one of `no`, `not`, `never`, `future`, `post-1.0`,
`deferred`, `planned`, `later`, `beyond`, `out of scope`,
`not implemented`, `not supported`, `not in v1`, `not yet`, `someday`,
`absent` or `missing`.

The scan is paragraph-based, ignores fenced code blocks and inline code, and
matches these terms case-insensitively as whole words:

- `generics stabilization`, `traits`, `shared ownership`,
  `kernel ownership contract`, `RCU-aware`, `kernel task backend`,
  `io_uring`, `growable stack`, `BSD`, `RISC-V`, `embedded profile`,
  `GPU`, `AI/HPC`, `enhanced c2crug`, `LSP`, `formatter`,
  `package ecosystem`.

The checker implements exactly this list; section 4 and
`scripts/check-docs.sh` change together.

## 5. Changing the manifest

Adding a document: add its row, add the `test -s` line to the Phase 23 gate
in `docs/crug/EXIT-GATES.md`, and rerun the gate.

Renaming or removing a required section: the change is a documentation
contract change. It needs the owning document edited in the same commit and an
amendment row in `docs/crug/EXIT-GATES.md` section 12 naming the gate that was
rerun.

Adding a required section to an existing page: add the heading to the page and
the row here in the same commit. The gate never passes on a manifest the tree
does not satisfy.
