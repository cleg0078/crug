# Crug tooling: the supported surface

Status: **Phase 20** (2026-10-20). This page records what the compiler and the
repository scripts provide to an editor, a debugger, a migration tool, or a
kernel build. It is the practical companion to ADR 0027 (tooling decisions),
`docs/crug/DIAGNOSTICS.md` (the code registry) and
`docs/crug/MIGRATION-EXAMPLES.md` (the executable migration examples).

The rule behind every section is the same: tooling reuses the compiler. There is
no second grammar, no second checker and no private diagnostic format. A tool
either runs `crugc`, reads a file the compiler wrote, or reads a documented
text format that the compiler or a script owns.

## 1. Compiler options and feature gates

The feature options are declared in `gcc/common.opt` and
`gcc/c-family/c.opt`, and their closure is computed in
`gcc/c-family/crug-common.cc`. The normative table is
`docs/crug/FLAGS.md`; the short version:

| Option | Enables | Implies |
| --- | --- | --- |
| `-fcrug-ownership` | `own`, `raw`; `move`, `drop`, `adopt`, `release`; `unsafe` | - |
| `-fcrug-borrow` | `ref`, `mut`; `borrow`, `borrow_mut` | ownership |
| `-fcrug-alias-facts` | the `restrict` fact from `mut`/`own` and proven call sites | borrow |
| `-fcrug-slices` | `slice<T>`, `slice (ptr, len)`, checked `s[i]` | - |
| `-fcrug-result` | `option<T>`, `result<T,E>`, `some`, `none`, `ok`, `err`, `match` | adt |
| `-fcrug-adt` | payload enums and `match` | - |
| `-fcrug-goroutines` | `go`, `yield`, `scope`, and Send/Sync at captures | - |
| `-fcrug-channels` | `chan T`, `channel`, `send`, `recv`, `close`, `select` | goroutines |
| `-fcrug-strict` | checking mode: every `-Wcrug-*` warning becomes an error | ownership, borrow |
| `-fcrug-ffi=<file>` | one `.crugffi` sidecar; repeatable | ownership |
| `-fcrug-ffi-discover` | discover sidecars next to quoted includes and in `CRUG_FFI_PATH` | ownership |
| `-fcrug-modern` | every Crug production | all eight feature flags |
| `-fcrug-kernel` | kernel mode; no `go`, `scope`, `chan` or `select` | excludes modern, goroutines, channels |
| `-fcrug-bounds-checks` / `-fno-crug-bounds-checks` | checked `s[i]` on a `slice<T>` (default on) | - |

The seven `-Wcrug-*` warnings are declared with `Init(1)` in
`gcc/c-family/c.opt`: `-Wcrug-unadopted-own`,
`-Wcrug-intrinsic-operand`, `-Wcrug-adopt-signature`,
`-Wcrug-jump-into-scope`, `-Wcrug-setjmp-drops`,
`-Wcrug-drop-unreachable` and `-Wcrug-returned-borrow`. The last one is
reserved: returning a borrow is an error while ADR 0018 D2 R7 stands, so the
option does not currently warn. With every feature flag off, the compiler emits
the object code GCC emits for the same C; that is a Phase 3 guarantee, not a
convenience.

Unknown `-fcrug-*` options are rejected by the driver. To see the surface a
given build accepts:

```sh
crugc --help=common | grep -- -fcrug
```

### 1.1 Dumps

`-fcrug-dump=<kind>` is a debugging option, not a feature flag. It is a
comma-separated list of four kinds and changes no generated code:

| Kind | File next to the output base | First line | Records |
| --- | --- | --- | --- |
| `ownership` | `<stem>.crug-ownership` | `crug ownership dump v1` | control-flow joins of an owning place, for example `join f.c:6: p=OWNED/EMPTY -> MAYBE_EMPTY` |
| `drop` | `<stem>.crug-drop` | `crug drop dump v1` | each inserted destruction and why |
| `borrow` | `<stem>.crug-borrow` | `crug borrow dump v1` | each loan and its last use |
| `alias` | `<stem>.crug-alias` | `crug alias dump v1` | `restrict` facts and the call sites proven to honour them |

The output base is the `-o` name; with no output base the input base is used.
A kind writes its file only when it has at least one line: a translation unit
with no join, destruction or loan produces no dump for that kind. For example, on
`examples/07-nll.c`:

```sh
gcc/crugc -O2 -fcrug-borrow -fcrug-dump=ownership,drop,borrow \
  examples/07-nll.c -o build/nll.o
cat build/nll.crug-drop
```

prints:

```text
crug drop dump v1
drop 07-nll.c:53: return moved via destroy_counter
```

and `build/nll.crug-borrow` holds:

```text
crug borrow dump v1
borrow 07-nll.c:41: r shares c
end 07-nll.c:45: r shares c (last-use)
borrow 07-nll.c:45: m excludes c
end 07-nll.c:50: m excludes c (last-use)
```

There is no `build/nll.crug-ownership` file here because this function has no
ownership join; the ownership dump writes a line only for a join. The first line
is the format version, so a consumer can reject an unknown dump before parsing
the rest. `OWNERSHIP.md` section 4, `DROP.md` section 6, `BORROW.md`
section 4 and `OPTIMIZATION.md` section 4 define the line grammars. The dumps
are build artifacts, not an incremental editor protocol: regenerate them when a
translation unit is compiled.

## 2. Machine-readable diagnostics

Every Crug error and warning has a stable code `crug-<AREA>-<NNN>` from the
registry in `docs/crug/DIAGNOSTICS.md`. The text form of the coded diagnostic
always ends with the code in brackets; an origin or fix note in the same group
is a separate diagnostic:

```text
examples/04-use-after-move.c:22:4: error: use of moved value 'a' [crug-UAM-001]
examples/04-use-after-move.c:21:20: note: value was moved here
```

The quotes are shown as ASCII apostrophes here; the compiler uses the
locale's quotation marks. The note is a separate diagnostic in the same
diagnostic group, not part of the error's message text, and it does not repeat
the code.

The machine-readable sink is SARIF, GCC's supported format:

```sh
gcc/crugc -O2 -fcrug-ownership -fsyntax-only \
  -fdiagnostics-add-output=sarif:file=build/uam.sarif \
  examples/04-use-after-move.c
jq -r '.runs[0].results[] | [.ruleId, .level, .message.text] | @tsv' \
  build/uam.sarif
```

The result is:

```text
crug-UAM-001	error	use of moved value 'a'
note	note	value was moved here
```

The stable code is the SARIF `ruleId`, and
`.runs[0].tool.driver.rules` carries the same id with a `helpUri` that
points at the registry. The message text is the human message without the code;
the text sink appends the bracket. Notes are separate results with
`ruleId "note"`, so a consumer that wants fix information must not filter
only for rule ids that start with `crug-`.

Three output controls matter:

- `-fdiagnostics-format=sarif-file` writes SARIF instead of text and lets
  GCC choose the file name next to the output base.
- `-fdiagnostics-add-output=sarif:file=PATH` keeps the text diagnostics and
  also writes SARIF to a known path. This is the form to use in a build system
  or an editor integration.
- `-fdiagnostics-set-output=sarif:file=PATH` replaces the sinks, so no text
  appears on stderr.

The code is identical in both forms; there is no numeric-only alternative and no
private JSON dialect. `DIAGNOSTICS.md` section 2 states the requirement that
the machine form carries the code and the note chain; SARIF meets it through
`ruleId` and the separate note results. The registry is checked mechanically
by `scripts/check-diagnostics.sh` (see section 9).

## 3. IDE and language-server groundwork

There is no LSP server in v1 (ADR 0027 D2). A future server has three documented
inputs and no reason to reimplement the front end:

1. **The compilation command for a translation unit.** GCC 16.2 in this tree
   does not emit `compile_commands.json` and has no `-MJ` option; a
   build integration has to synthesize an entry from the command line that
   produced the object. The entry needs `directory`, `file` and
   `arguments`, and `arguments` must include the exact `-fcrug-*`
   set, the `-I` paths and the `-D` defines. A missing feature flag is
   not a cosmetic difference: the same bytes are a different language.
2. **Diagnostics for the current edit.** The server runs `crugc` with
   `-fsyntax-only` and `-fdiagnostics-add-output=sarif:file=...`, then
   maps each SARIF result to a range. The code in `ruleId` is the stable key
   for a documentation link or a suppression list; the text is the message a
   human reads.
3. **Semantic overlays from the dumps.** `-fcrug-dump=ownership,drop,borrow,alias`
   gives the state at build time. The dumps are not a query API and are not
   designed for every keystroke; they are appropriate for a save hook, a build
   annotation or a code review helper.

The mapping is deliberately small:

| Editor feature | v1 source of truth | Limit |
| --- | --- | --- |
| diagnostics, error codes | SARIF or text from `crugc` | no fix edits; the fix is prose in a note |
| hover on a Crug construct | this page, `FLAGS.md`, the construct's document | no per-symbol query API |
| semantic tokens, inlay hints | none in v1 | a dump is a build artifact, not an incremental protocol |
| go to definition, references | none in v1 | the front end's symbol tables are not exposed |
| code actions | message and note text | read-only suggestions in v1 |
| formatting, rename | none in v1 | see section 5; rename needs the front end |
| syntax highlighting | a lexical grammar (section 4) | best effort; Crug words are contextual |

A future LSP server must either drive `crugc` or reuse the front-end sources
in its own build; it must not carry a second parser, a second borrow checker or a
hand-written grammar that claims to be authoritative. That is the D6 rule in ADR
0027, and it is also what keeps a server from blessing a program the compiler
rejects.

## 4. Syntax highlighting

No grammar is shipped in v1. If one is added, it must be a valid JSON TextMate
grammar under `editors/` (for example `editors/crug.tmLanguage.json`) and
it must use these scopes:

| Words | Scope |
| --- | --- |
| `own` | `storage.modifier.ownership.crug` |
| `ref`, `mut` | `storage.modifier.borrow.crug` |
| `raw` | `storage.modifier.raw.crug` |
| `unsafe` | `keyword.other.unsafe.crug` |
| `move`, `adopt`, `drop`, `release`, `borrow`, `borrow_mut` | `keyword.operator.ownership.crug` |
| `go`, `scope`, `select`, `match` | `keyword.control.crug` |
| `yield` | `keyword.control.flow.crug` |
| `chan` | `storage.type.channel.crug` |
| `channel`, `send`, `recv`, `close` | `support.function.channel.crug` |
| `some`, `none`, `ok`, `err` | `support.function.constructor.crug` |
| `option`, `result`, `slice` | `support.type.crug` |

The C base grammar supplies comments, strings, numbers, types and preprocessor
rules; a Crug grammar adds words, not a new lexical structure. Crug words are
recognized contextually (ADR 0002), so a lexical grammar cannot tell a variable
named `own` from the qualifier. Highlighting is best effort; it is not a
correctness claim and never a substitute for `crugc`. There is no
Tree-sitter grammar and no semantic highlighter in v1.

## 5. Formatter

There is no formatter in v1 (ADR 0027 D3). `c2crug` is a migration tool, not
a formatter: in `--safe` mode it inserts `own ` at a declaration and
replaces a proven `free (p);` with `drop (p);`, keeping the line count
unchanged and touching nothing else. It preserves the author's layout on
purpose, so a migration diff stays reviewable.

Do not run `clang-format`, GNU `indent` or a C formatter on a Crug
translation unit: none of them knows the contextual words or the ownership
grammar, and a formatter that moves a `drop` or an `own` changes meaning.
A future formatter has to consume the compiler's token stream, be idempotent,
and preserve comments and line structure; that decision is not made in v1.

## 6. Debugger notes

GDB and LLDB see the program as C. An `own T *` is an ordinary `T *` in
DWARF, a destructor is an ordinary function call, and the ownership state lives
only in the compiler (ADR 0003). There is no ownership or borrow pretty-printer
and no debugger front-end in v1 (ADR 0027 D5).

What this means in practice:

- Build with `-O0 -g` when stepping through destruction. At `-O2` a
  destructor call or a task frame may be inlined, moved or elided.
- Put a breakpoint on the destructor function to see a destruction; the call is
  direct and has the user-written signature.
- To inspect the analysis, use the compile-time dumps:
  `gcc/crugc -O0 -g -fcrug-borrow -fcrug-dump=borrow,drop -c file.c` and read
  `file.crug-borrow` and `file.crug-drop`. The debugger cannot show a
  loan that the compiler has already discharged.
- A `go` task is not an OS thread. The runtime is M:N (ADR 0009), so the
  debugger's thread list is the worker set, not the logical task set; a task
  stack is a runtime-managed stack. A backtrace at a breakpoint inside a task
  shows the switch entry point and the task frame, not a pthread per task.
- A `chan T` is one pointer to a runtime object; there is no channel
  pretty-printer. `chan own T` ownership is an analysis fact and does not
  appear in the object.

## 7. FFI tooling

The FFI surface is the `.crugffi` sidecar and the `c2crug` migration tool.
The compiler reads a sidecar when it is named with `-fcrug-ffi=<file>` (or
found by `-fcrug-ffi-discover`) and checks every call against it. A sidecar
starts with `crugffi 1.0;`; a small example:

```text
crugffi 1.0;
type opaque Node;
fn make_node returns owned Node drop free_node;
fn free_node consumes arg0 nullable;
fn peek borrows arg0;
fn set_value mut_borrows arg0;
```

The clauses are `returns owned T`, `returns int`/`void`, `drop D`,
`consumes argN`, `borrows argN`, `mut_borrows argN`, `out_owned argN`,
`nullable`, `callback argN`, `retained argN until X`, `blocking`,
`thread_affinity X`, and the type clauses `opaque`, `send`/`!send` and
`sync`/`!sync`. `docs/crug/FFI.md` sections 1 and 5 give the normative
grammar and the limits. A destructor is resolved from the `drop` clause on
the function, then from the type declaration, otherwise it is an error.

The sidecar is validated by the compiler, not by a separate tool. The parse and
contract failures use the registered codes:

| Code | Meaning |
| --- | --- |
| `crug-FFI-001` | the sidecar does not parse (header, semicolon, character, keyword) |
| `crug-FFI-002` | a type clause names `void` |
| `crug-FFI-003` | unknown or repeated clause, malformed argument index |
| `crug-FFI-004` | conflicting declarations |
| `crug-FFI-005` | argument index out of range |
| `crug-FFI-006` | the declaration does not fit the C prototype, or the name is not declared |
| `crug-FFI-010` | the sidecar exceeds the size or entry limit |
| `crug-FFI-011` | the destructor cannot be resolved |
| `crug-FFI-012` | a NUL byte appears inside a metadata line |

The call-site checks use `crug-FFI-007` (`consumes` argument not moved),
`crug-FFI-008` (`out_owned` not the address of an empty owned variable) and
`crug-FFI-009` (`callback` argument not a function or null). All of them
are errors in hybrid and strict mode, and all are registered in
`docs/crug/DIAGNOSTICS.md`. An explicit sidecar that cannot be read, cannot be
parsed or declares a function that is not in the translation unit is an error,
never silently ignored.

`c2crug` is the migration helper. It takes a directory, has three modes and
never writes a sidecar:

```sh
./c2crug --analyze src/ --report build/file.json
./c2crug --safe src/ -o build/src-crug
./c2crug --diff src/
```

`--analyze` writes nothing but its report, `--safe` writes a copy with the
proven edits applied and refuses an output that already exists, and `--diff`
prints a unified diff. `--cpp PATH` selects the host C preprocessor used to
judge eligibility (default `cc`), and `--cpp-arg ARG` passes it one extra
argument and may be repeated. A function is rewritten only when its declaration
prefix and release call are token-identical between the source and the
preprocessed text, and preprocessing adds or removes no control-flow exit
token; every entry records `preprocessed` and `eligibility`, and an
ineligible entry is raw with confidence 0. A `.crugffi` sidecar is used only
when its first non-empty line is `crugffi <major>.<minor>;` with major
exactly 1; otherwise it is ignored with `c2crug: ignoring sidecar ...` on
stderr. The proof, the confidence values, the report schema
`docs/crug/c2crug-report.schema.json` and the corpus are in
`docs/crug/C2CRUG.md`. The tool never invents ownership: an unproven pointer
stays raw and the report says why. Two checks cover it:
`python3 scripts/test-c2crug.py` (140 checks) and `scripts/check-c2crug.sh`;
the Makefile target is `make check-C2CRUG`.

## 8. Kernel tooling

Kernel mode is plain GNU C in v1 (ADR 0017): no kernel ownership contracts, no
`go`, no `chan`, no `select`. The tooling exists to prove that the
compiler accepts and does not perturb the pinned Linux tree, not to add a
language feature to it. The pin and the stages are in
`docs/crug/KERNEL-BASELINE.md`, `kernel/KERNEL-CORPUS.md` and
`EXIT-GATES.md` phase 19.

| Tool | Purpose | Usage |
| --- | --- | --- |
| `scripts/kernel-stage.sh` | run one stage K1-K7 or all in order | `scripts/kernel-stage.sh K4` |
| `scripts/kernel-object-diff.sh` | compare the pinned corpus built through `crugc` with the wrapped GCC driver | `scripts/kernel-object-diff.sh` |
| `scripts/check-p0-open.sh` | parse the known-issue ledger; no open P0, every fixed P0/P1 names a regression | `scripts/check-p0-open.sh [--kernel]` |

The stages are K1 selected headers, K2 twelve translation units, K3 all of
`lib/`, K4 the 438-object x86_64 corpus, K5 the full tree (vmlinux, bzImage,
System.map), K6 the qemu boot smoke test, and K7 the 15-object aarch64 subset.
Environment variables are `KSRC` (default `build/linux-6.18.57`),
`KO` (default `build/kernel-crug`), `KO_ARM`,
`CRUGC` (default `gcc/crugc`), `CROSS` and `JOBS`. Exit 0 is a
pass, 1 a failed check, 2 a missing prerequisite or bad invocation; the kernel
tree itself is never modified.

`scripts/kernel-object-diff.sh` compiles the same pinned corpus twice, once
with `CC=gcc/crugc` and once with the wrapped GCC driver, and compares every
object byte for byte. A difference is allowed only when
`kernel/object-diff-allow.txt` has a line with the object path and a one-line
explanation; an empty allow file is the expected state. The script exits 0 when
every object is identical or explained, 1 for an unexplained difference or a
missing object, and 2 for a missing prerequisite.

`scripts/check-p0-open.sh` reads the ledger region of
`docs/crug/KNOWN-ISSUES.md`. Without arguments it covers every scope; with
`--kernel` it covers the kernel scope only. It exits 0 when no P0 is open and
every fixed P0/P1 names a regression, 1 on an open P0 or a missing id, and 2 on
a malformed ledger or a usage error.

`kernel/KERNEL-CORPUS.md` is the corpus definition, not a hand-picked list:
K4 is every object kbuild produces directly in the nine listed directories for
the pinned `x86_64_defconfig` (438), K7 is the aarch64 subset (15), and K3 is
the 106 objects directly in `lib/`. The lists are regenerated from the pinned
tree; a changed list is a corpus amendment. The corpus never enters the
repository.

## 9. The checks

The scripts are the authoritative implementation of the Phase 20 checks; the
gate wording is in `EXIT-GATES.md` phase 20.

| Check | Command | Covers |
| --- | --- | --- |
| registry | `scripts/check-diagnostics.sh` | every referenced `crug-<AREA>-<NNN>` is registered once and uses a known area |
| docs examples | `make check-docs-examples` (runs `scripts/check-docs-examples.sh`) | each marked example compiles, runs, and prints exactly the expected stdout |
| migration tool | `make check-C2CRUG` | the c2crug unit checks and the migration corpus |
| examples | `make examples` | every accepted example builds; the two rejected programs fail with their documented code |

The docs example check is small enough to run on every change: on a warm tree it
reads `docs/crug/MIGRATION-EXAMPLES.md`, compiles six programs under
`build/docs-examples`, runs them, and prints, for example:

```text
docs-examples: 6 example(s), 0 failure(s)
```

A failure names the example and prints the compiler output or a unified diff of
the expected and actual stdout. Exit 1 is a content failure; exit 2 is a setup
error (missing page, compiler or runtime object, or a malformed block).

## 10. See also

- `docs/crug/FLAGS.md`: the full option table and the feature macros.
- `docs/crug/DIAGNOSTICS.md`: the registry, the areas and the message rules.
- `docs/crug/MIGRATION-EXAMPLES.md`: the checked before/after programs.
- `docs/crug/FFI.md` and `docs/crug/C2CRUG.md`: the sidecar grammar and
  the migration proof.
- `docs/crug/KERNEL-BASELINE.md` and `kernel/KERNEL-CORPUS.md`: the pin
  and the corpus.
- `docs/crug/adr/0027-tooling-decisions.md`: the decisions behind this page.
