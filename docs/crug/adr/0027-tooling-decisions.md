# ADR 0027 - Tooling: stable diagnostics, editor support, and executable documentation

- **Status:** Proposed
- **Date:** 2026-10-20
- **Deciders:** project owner (the v1 tooling scope and what may be promised),
  compiler maintainer (the diagnostic and dump surfaces), documentation
  maintainer (the migration examples and the tooling page)
- **Relates:** ADR 0002 (contextual keywords), ADR 0003 (metadata storage),
  ADR 0005 (drop lowering), ADR 0007 (FFI metadata), ADR 0018 (grammar),
  ADR 0021 (strict mode), ADR 0023 (c2crug), ADR 0025 (channels),
  ADR 0026 (Send/Sync), MASTER_SPEC sections 37-39,
  CRUG_PRODUCTION_ROADMAP section 25, `docs/crug/TOOLING.md`,
  `docs/crug/DIAGNOSTICS.md`, `docs/crug/MIGRATION-EXAMPLES.md`

## Context

Phase 20 asks that Crug be usable without compiler-internals expertise. The
compiler already produces a stable code with every diagnostic, the GCC driver
already has a SARIF sink, and the front end already writes ownership, drop,
borrow and alias dumps. What the phase does not have is a decision that says
which of those surfaces is the contract, which tools are deliberately not built,
and what an editor or a debugger may rely on.

The pressure to add a language server is real but the wrong response in v1 is a
second parser. A server with its own grammar would disagree with the compiler
about contextual keywords, macro expansion and the ownership rules, and the
disagreement would surface as an editor that accepts a program the compiler
rejects. The same argument applies to a formatter: a layout tool that does not
understand `move`, `drop` or a task scope cannot safely rewrite a Crug
translation unit.

The phase also has to keep its documentation honest. A migration example that
does not compile is worse than no example, because it teaches a rule the compiler
does not implement. The decision below therefore treats the examples as tests,
not as prose.

## Decision

### D1 - The stable code is the machine-readable contract

Every Crug error and warning carries a stable `crug-<AREA>-<NNN>` code from
`docs/crug/DIAGNOSTICS.md`. The text form of the coded diagnostic ends with the
code in brackets;
the SARIF form carries the same string as `ruleId`, and the driver's rule
metadata carries it with a `helpUri` to the registry. The message text does
not contain the code, so the code is not duplicated in prose; a consumer that
reads only `ruleId` gets exactly the same identity. Notes
(fix suggestions and origin notes) are separate results in the same diagnostic
group with `ruleId "note"`, not a private note array.

There is no new diagnostic format. GCC's SARIF sink is the machine form, and the
text form remains the human form. A numeric-only or file-local identifier is
forbidden: logs, suppressions and documentation links must survive a rebuild.
The code is allocated with its registry row, its origin rule and its test under
`gcc/testsuite/crug.dg/diagnostics/`, as the registry rules in
`docs/crug/DIAGNOSTICS.md` require.

### D2 - There is no LSP server in v1, but the consumption contract is documented

The phase ships no language server, no editor plugin and no daemon. Instead
`docs/crug/TOOLING.md` fixes what a future server may consume:

- a per-translation-unit compilation command in `compile_commands.json`
  form, including the exact `-fcrug-*` set, include paths and defines. GCC
  16.2 in this tree does not emit the file and has no `-MJ` option, so the
  build integration synthesizes the entry; the entry is the only authoritative
  statement of which dialect a file is compiled in.
- SARIF from `crugc -fsyntax-only -fdiagnostics-add-output=sarif:file=...`
  for diagnostics, with `ruleId` as the stable key and the note results as
  the fix information.
- the files written by a comma-separated
  `-fcrug-dump=ownership,drop,borrow,alias` for a semantic overlay at save or
  build time. They are build artifacts, not an incremental protocol.

The page also fixes the feature-to-surface mapping (diagnostics, hover, semantic
tokens, definition, code actions, formatting, highlighting) so a future server
does not invent a contract by accident. A server that is built later must drive
`crugc` or reuse the front-end sources; it must not carry a second parser or
a second borrow checker. No completion, rename or semantic-token claim is made
in v1.

### D3 - There is no formatter in v1

The phase ships no formatter and no formatting style. `c2crug --safe` is a
migration rewrite, not a formatter: it inserts `own ` at a declaration and
replaces a proven `free (p);` with `drop (p);` while keeping the line
count and every other byte of layout unchanged. That is deliberate; a migration
diff must stay reviewable.

Running `clang-format`, GNU `indent` or a C formatter on a Crug file is
forbidden as a supported workflow: none of them knows the contextual words, and
moving an `own`, a `drop` or a task scope can change meaning. A future
formatter must consume the compiler's token stream, be idempotent, preserve
comments and line structure, and be tested against the same corpus; no such
decision is made here.

### D4 - Syntax highlighting is a lexical scope set, not a parser

The v1 highlighting decision is a scope table for the Crug words, recorded in
`docs/crug/TOOLING.md` section 4: `own`, `ref`, `mut`,
`raw`, `move`, `adopt`, `drop`, `release`, `borrow`,
`borrow_mut`, `go`, `scope`, `select`, `match`, `yield`,
`chan`, `channel`, `send`, `recv`, `close`, `some`,
`none`, `ok`, `err`, `option`, `result`, `slice` and
`unsafe`. A TextMate grammar is optional; if one is added it must be valid
JSON under `editors/` and use those scopes.

The grammar is lexical only. Crug words are recognized contextually (ADR 0002),
so a variable named `own` may be coloured as the qualifier. That is accepted,
because highlighting is not a correctness claim. A Tree-sitter grammar, a
semantic-token provider or a highlighter that infers ownership is out of scope
for v1.

### D5 - Debugger support is notes, not a front end

GDB and LLDB see plain C: an `own T *` is a `T *`, a destructor is a
direct call, and ownership and borrow state are not in DWARF. The phase ships no
debugger plugin, no pretty-printer and no custom DWARF. The documented workflow
is to build with `-O0 -g`, break on the destructor when a destruction
matters, and use `-fcrug-dump` at compile time when the analysis, not the
machine state, is the question.

The notes also state the task limit: `go` is not one OS thread per task
(ADR 0009), so the debugger's thread list is the worker set, not the logical
task set, and a `chan T` is one runtime pointer. The compiler must not be
changed to emit hidden ownership metadata merely to make a debugger view
prettier; ADR 0003 forbids the hidden state.

### D6 - Tooling reuses the compiler and the scripts; there is no second parser

Every supported tool takes the compiler as the source of truth. The compiler
produces diagnostics, dumps, objects and the runtime interface; the scripts
orchestrate it; `c2crug` uses the same proof rules and reads the same
sidecars. No tool may ship an independent grammar, an independent borrow checker
or an independent diagnostic code table, and no tool may bless a program that
`crugc` rejects. A tool that needs syntax or semantics either invokes
`crugc` or reuses the front-end sources; it does not re-derive them.

This also fixes where a new check belongs: a new user-facing check is a script
under `scripts/` that drives the compiler, and a new stable fact is a
compiler output or a registry row, not a private file format.

### D7 - Documentation examples are executable and checked

`docs/crug/MIGRATION-EXAMPLES.md` marks each executable program with an HTML
comment, a `c` fenced block and an expected `text` block.
`scripts/check-docs-examples.sh` extracts every marked program, compiles it
with the compiler in this tree, runs it, and compares stdout byte for byte. The
runtime examples build and link the `libcrug` Linux objects. The check exits
0 with a summary of the form `docs-examples: N example(s), 0 failure(s)`,
1 on a content failure, and 2 on a setup error.

An example in that page is therefore a test, not a snippet. A change to the
compiler that breaks an example fails the gate until the page or the compiler is
fixed; a new construct that a user needs gets an example that runs. The check is
part of the Phase 20 gate (`EXIT-GATES.md` phase 20) and is exposed as
`make check-docs-examples`; the script is the implementation the target runs.
Hand-written examples that are not marked and not run are not part of this
contract.

## Consequences

- An editor integration can be built against documented inputs without waiting
  for a language server: SARIF gives diagnostics and stable codes, a synthesized
  `compile_commands.json` gives the dialect and flags, and the dumps give a
  semantic overlay. The cost is latency: the editor runs a compiler, not a
  daemon, and the dumps are regenerated per build.
- There is no completion, hover-with-types, rename, semantic token, code action
  edit or formatting in v1. The page says so; a future server can add them
  without changing the compiler contract, because it must reuse the front end.
- The SARIF contract works with existing SARIF viewers, but notes are separate
  results with `ruleId "note"`. A consumer that assumes every result has a
  `crug-*` rule id will silently drop the fix text; the documentation says
  this explicitly.
- A lexical grammar can over-highlight a contextual word. That is accepted as a
  cosmetic limitation, not a correctness bug; the compiler stays the authority.
- The examples add a check to the tree and a runtime link for the concurrency
  example, and they make the documentation part of the build's evidence. A
  deliberately small example set is preferred over a large unverified one.
- No ABI, object-code or runtime change follows from this ADR. The tooling is
  text and process around the existing compiler, not a new compiler mode.

## Alternatives considered

- **Ship an LSP server in v1.** Rejected: it needs a second parser or a large
  refactor of the C front end into a library, and Phase 20 has no budget for
  either. The documented SARIF and dump contract gives most of the value with no
  new process to maintain.
- **Define a private JSON diagnostic format.** Rejected: GCC already emits
  SARIF, every code is a `ruleId`, and a second format would need its own
  versioning and its own consumers. The text form remains the fallback.
- **Ship a formatter or bless `clang-format`.** Rejected: layout tools that
  do not understand ownership can change meaning, and a C formatter cannot parse
  the contextual words reliably. A formatter is a later decision with its own
  ADR.
- **Ship a complete TextMate or Tree-sitter grammar as the highlighter.** The
  TextMate scope table is accepted as optional lexical support; a Tree-sitter
  grammar and a semantic highlighter are deferred because they would be a second
  parser in all but name.

## Evidence

All evidence is from the Phase 20 tree on 2026-10-20 with GCC 16.2.0.

- `scripts/check-docs-examples.sh` printed
  `docs-examples: 6 example(s), 0 failure(s)` and exited 0; the six marked
  programs are the before/after pairs in `docs/crug/MIGRATION-EXAMPLES.md`.
  The runtime example links `libcrug/build/linux/task.o`,
  `pal_linux.o`, `switch.o`, `stack.o` and `chan.o` with
  `-pthread`.
- `gcc/crugc -O2 -fcrug-ownership -fsyntax-only
  -fdiagnostics-add-output=sarif:file=build/uam.sarif
  examples/04-use-after-move.c` produced the text
  `examples/04-use-after-move.c:22:4: error: use of moved value 'a'
  [crug-UAM-001]` and the note `value was moved here`. The SARIF result
  carried `"ruleId": "crug-UAM-001"`, the note result carried
  `"ruleId": "note"`, and `tool.driver.rules` carried the id with its
  `helpUri`.
- `gcc/crugc -O2 -fcrug-borrow -fcrug-dump=ownership,drop,borrow
  examples/07-nll.c -o build/nll.o` wrote `build/nll.crug-drop` with
  `drop 07-nll.c:53: return moved via destroy_counter` and
  `build/nll.crug-borrow` with the four `borrow`/`end` lines shown in
  `docs/crug/TOOLING.md`; the ownership dump was absent because that
  function has no join.
- `scripts/check-diagnostics.sh` printed
  `check-diagnostics: 129 registered code(s); 129 referenced code(s); all
  references registered.` and exited 0.
- `scripts/check-c2crug.sh` and `python3 scripts/test-c2crug.py` are the
  migration-tool evidence; `make check-C2CRUG` is their target.
- The diagnostic corpus and its goldens are
  `gcc/testsuite/crug.dg/diagnostics/top50.txt` and
  `gcc/testsuite/crug.dg/diagnostics/{cases,golden}/`. The tooling page is
  `docs/crug/TOOLING.md`; the checked examples are
  `docs/crug/MIGRATION-EXAMPLES.md`.
