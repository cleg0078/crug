# ADR 0007 - C FFI Metadata: the .crugffi v1 Format

**Status:** Accepted (2026-10-19)
**Date:** 2026-10-19
**Ratification:** 2026-10-19 by CLEG0078 (project owner)
**Related:** ADR 0003 (ownership metadata storage), ADR 0004 (aggregate ownership and adoption), ADR 0005 (drop lowering), ADR 0006 (C ABI policy, `docs/crug/ABI.md`), ADR 0018 (surface grammar), RFC-0001 sections 23-24, MASTER_SPEC section 16, RUST_MEMORY_GAPS section 26, ROADMAP Phase 9, `docs/crug/DIAGNOSTICS.md`
**Supersedes:** nothing. The conceptual sketches in MASTER_SPEC section 16.3 and RUST_MEMORY_GAPS section 26 are examples, not a format.

## Context

Crug has three C integration levels (MASTER_SPEC section 16.2):

- **Level 0, unmanaged C.** `Foo *x = foo_create(); foo_destroy(x);` The pointer is raw and no ownership is claimed.
- **Level 1, local adoption.** `own Foo *x = adopt(foo_create(), foo_destroy);` The programmer states the contract at the call site.
- **Level 2, metadata.** `own Foo *x = foo_create();` The library states the contract once, in a file that ships with it.

Level 2 is the only level that scales to real libraries, and it is the only level that lets a wrong contract be written once and believed everywhere. Phase 9 therefore cannot start until this format is written down, and the format cannot be "whatever the parser does" without becoming unfalsifiable.

The format is constrained by decisions already frozen elsewhere:

- Ownership is static metadata, never a hidden field, hidden bit, registry or runtime call (ADR 0003 D5).
- Qualifiers are erased at every ABI boundary (ADR 0006 D3); metadata may not change layout or codegen (ADR 0006 D2, D4).
- A copying operation of an owning value is a compile-time diagnostic, not a runtime check (ADR 0003 D2).
- Adoption already exists as source syntax (ADR 0004 D8), so metadata is an optimisation of the programmer's bookkeeping, not a new capability of the language.
- Existing `.a` and `.so` files must never need recompilation (RFC-0001 sections 23-24).
- Metadata must be versioned, and unknown mandatory semantics must be rejected (RFC-0001 section 24).
- Ownership must never be inferred from function naming or heuristics (MASTER_SPEC section 16.4, RFC-0001 section 23).

## Decision

Define one textual sidecar format, `.crugffi`, version 1.0, with a small, closed clause set. It describes ownership, borrowing, destructors, out-parameters, nullability, callbacks, blocking and thread affinity for C functions whose declarations the compiler already has. It cannot describe layout, cannot add code, cannot load at run time, and cannot be extended by a compiler-specific clause without a format revision.

The rest of this ADR is normative for the parser that Phase 9 implements.

### D1 -- What a sidecar is

A `.crugffi` file is UTF-8 text (a byte-order mark is rejected; CRLF line endings are accepted), line-oriented, whitespace-insensitive except inside names, with `#` line comments and blank lines. It contains a version header and a sequence of declarations. It is not C, it has no preprocessor, no includes, no expressions, no strings beyond identifiers, and no generated code. There is exactly one such file per library (plus optional per-header files, see D5); a file may describe any number of functions and types.

The file annotates declarations, it does not introduce them. A `fn` entry takes effect only when the translation unit has an external declaration of that identifier. Nothing in the file changes a declaration's type, its calling convention, its symbol name, its layout or its debug information.

### D2 -- Grammar v1

```text
file        := header item*
header      := 'crugffi' version ';'
version     := INT '.' INT
item        := type-decl | fn-decl

type-decl   := 'type' c-type clause* ';'
c-type      := [ 'struct' | 'union' | 'enum' ] IDENT | 'void'
fn-decl     := 'fn' IDENT clause* ';'

clause      := 'returns' ret
             | 'consumes' argref
             | 'borrows' argref
             | 'mut_borrows' argref
             | 'out_owned' argref
             | 'nullable' argref
             | 'drop' IDENT
             | 'callback' argref
             | 'retained' argref 'until' IDENT
             | 'blocking'
             | 'thread_affinity' IDENT
             | 'opaque'
             | 'send' | 'sync' | '!send' | '!sync'

ret         := 'owned' c-type [ 'nullable' ] [ 'drop' IDENT ]
argref      := 'arg' INT | 'ret'

INT         := [0-9]+          (decimal, no sign, no leading '+' )
IDENT       := C identifier    (ASCII letters, digits, underscore; not starting with a digit)
```

Lexical and structural rules:

1. The header must be the first non-comment, non-blank content: `crugffi 1.0;` Keywords are lowercase and case-sensitive; identifiers are case-sensitive.
2. `arg0` is the first declared parameter, counted from zero; `ret` denotes the return value. An `argN` whose N is not less than the number of named parameters is an error. Variadic parameters have no index and cannot be annotated in v1.
3. `c-type` in a v1 sidecar is a bare identifier, or `struct`/`union`/`enum` followed by a tag, or `void`. **Pointers are not written at all**: a `returns` clause names the pointee type of the returned pointer (`returns owned void` describes `malloc`, `returns owned FILE` describes `fopen`), and an `out_owned` argument is named by the type the callee writes through the parameter, whose pointer-to-pointer level follows from the declaration. This removes any way for a sidecar to disagree with the header about pointer structure, and removes the `Foo *` versus `Foo*` question from the format. A file that writes a star is rejected with a syntax error that says so.
4. Letters in `!send` and `!sync` are part of the token; `!` is not otherwise legal.
5. Every declaration ends with `;`. A clause may not be repeated on one declaration, except that `drop` may appear once on a `returns` clause and once at declaration level (the two spellings are two ways to say the same thing; stating both is an error). `type void` is an error.
6. No clause may be attached to a declaration of a `static` (internal-linkage) function; in an explicitly named file that is an error, in a discovered file the entry is ignored.
7. `returns` belongs to a `fn` declaration; `opaque`, `send`, `sync`, `!send` and `!sync` belong to a `type` declaration. Using one in the other position is an error, so the grammar above is read as two clause kinds, not one.
8. No other construct exists. An unknown keyword, a missing `;`, a nested construct or a directive is a syntax error, not a comment and not an extension point.

### D3 -- Clause semantics

Every clause is a statement about the callee's behaviour. The compiler validates the shape of the statement against the declaration; it cannot verify the behaviour, and it does not try (D8).

| Clause | Meaning for the caller | Static obligations checked |
| --- | --- | --- |
| `returns owned T` | The returned pointer is an owned value. It may be bound to `own T *` directly; the binding is move-only and is dropped at end of scope. | The declaration must return a pointer to `T` (both types must be the same type under ADR 0006 erasure). If no destructor is stated by the clause or by a `type` declaration for `T`, the metadata is rejected. |
| `drop D` on a `returns owned` or on a `type` | `D` is the destructor for the owned value. | `D` must be declared with external linkage as `void D(T *)`, exactly as ADR 0004 D8 requires for `adopt`. A different signature, a non-function, a variable or a static function is an error. |
| `consumes argN` | The callee takes ownership of the argument. The caller must write `move(x)` or pass an owned prvalue; the argument is MOVED afterwards and must not be used or dropped by the caller. | The argument must be a pointer to the same type as the value passed (after erasure); passing a shared borrow is an error. `nullable` may be combined: a null owned pointer may be passed (e.g. `free(NULL)`). |
| `borrows argN` | The callee reads through the argument for the duration of the call and does not keep it. | The argument must be a pointer; a caller's live mutable borrow of the same object conflicts, as does moving or dropping the operand across the call. |
| `mut_borrows argN` | The callee may read and write through the argument for the duration of the call and does not keep it. | The argument must be a pointer; no other live borrow and no move/drop of the operand across the call. |
| `out_owned argN` | The callee writes an owned value through the pointer argument. The caller declares an uninitialised `own T *` object, passes its address, and may use the binding after the call. | The parameter must be a pointer to a pointer to `T`; the same type equality rules as `returns owned` apply, and the destructor must be determinable. The contract asserts initialisation on **every normal return**; see D9 for why functions that can fail without initialising are not annotatable in v1. |
| `nullable` inside a `returns` clause, or `nullable argN` | The pointer may be null. On a return it also permits a null `own` binding, which the drop machinery skips (ADR 0004 D8). | The annotated operand must be a pointer. Absence of `nullable` is not a non-null claim the compiler can enforce; v1 has no pointer-nullness analysis, so this clause is a documented contract (D9). |
| `callback argN` | The callee may call the function pointer only during the call, and must not store it. | The argument must be a function pointer. |
| `retained argN until F` | The callee stores the pointer and may use it until `F` is called. | The argument must be a pointer or function pointer; `F` must be declared. Recorded and enforced only as far as D9 allows; the clause is mandatory to state because later lifetime checking must not require editing shipped metadata. |
| `blocking` | The call may block. | None. Placeholder until P17; recorded so scheduler work has data. |
| `thread_affinity NAME` | The call has an affinity contract identified by `NAME` (a free-form identifier such as `render` or `audio`). | None. Placeholder until P17; recorded. |
| `opaque` on a `type` | The type has no C definition available to Crug: it may only be held as an owned pointer, never by value, copied or inspected. | The type must be a tag or a typedef name; `opaque` on `void` is an error. |
| `send`, `sync`, `!send`, `!sync` on a `type` | Concurrency properties of the type (MASTER_SPEC section 28). | Mutually exclusive pairs; re-stating the same property is an error. Recorded; enforcement belongs to the concurrency phases. |

Destructor resolution order for `returns owned T` is: the clause's `drop`, else a `type` declaration for `T` with a `drop`, else reject. There is no fallback to a similarly named function.

### D4 -- Conflicts and determinism

The parser validates the whole merged metadata set (D5) before it annotates anything. These are errors, reported in a deterministic order (see D5 for file order, then declaration order, then clause order):

- a clause that contradicts another clause on the same operand: `borrows` with `mut_borrows`, `borrows` or `mut_borrows` with `consumes` or `out_owned`, `callback` with `out_owned`, `nullable` on a non-pointer;
- two different `returns owned` destructors for the same function, or a clause destructor that differs from the `type` destructor;
- two `fn`/`type` declarations for the same name whose clauses differ in any way (identical duplicates are allowed and merged, which is what makes overlapping adjacent files harmless);
- a destructor or `until` function that is not declared, or has the wrong signature;
- an `argN` out of range, or an annotation of a variadic parameter;
- a file whose header is missing, has a major version other than 1, or is a minor version whose clauses are not all known to this compiler (D6).

An `opaque` type that also has a definition visible in the translation unit is not a conflict: `opaque` is a promise about Crug's view of the type (no by-value use, no copies), and a translation unit that happens to see the definition does not change the ownership contract.

A single diagnostic is emitted per conflict, naming the file, the line, the function or type, both offending clauses, and the stable code allocated when the parser is implemented. Determinism is a tested property, not an aspiration: Phase 9's gate repeats the failing invocation and requires byte-identical stderr.

No clause in v1 overrides another. There is deliberately no `override`, no last-file-wins rule, and no way to silence a conflict from a sidecar.

### D5 -- Discovery and selection

The compiler considers, in this order:

1. every `-fcrug-ffi=<path>` given on the command line, in command-line order (repeatable);
2. adjacent files for headers the translation unit included with a quoted form: `#include "foo.h"` looks for `foo.crugffi` in the same directory the header was found in;
3. nothing for angle-form includes: an `#include <foo.h>` directory is not under the program's control, so a file appearing next to a system header must never change a program's ownership (see D8); such headers are annotated only by an explicit `-fcrug-ffi=<path>` or by a system metadata directory;
4. system metadata: for a header `foo.h`, the file `foo.crugffi` in each directory of `CRUG_FFI_PATH` (colon-separated) and then in the configured `$prefix/share/crug/ffi` directory, in that order;
5. otherwise no metadata: the declaration stays unmanaged (Level 0) and can still be used with `adopt` (Level 1).

Metadata is keyed by header, not by library: annotating `sqlite3.h` means naming the file `sqlite3.crugffi`. Files merge; identical duplicates merge silently, differing duplicates are a conflict (D4). There is no include directive, no glob, no search-path override per file, and no run-time lookup.

`-fcrug-ffi=<path>` implies `-fcrug-ownership` (metadata presupposes the ownership feature) and defines `__CRUG_FFI_VERSION__` as the integer `MAJOR * 100 + MINOR`, so v1.0 is `100` (RFC-0001 Appendix B already lists the macro). Under `-fcrug-strict`, discovery steps 2-4 are disabled: every sidecar must be named explicitly, so a program's ownership semantics cannot be changed by files appearing next to a header. This is a deliberate strict-mode tightening and is testable. It extends the strict-mode behaviour defined in ADR 0018 D1 and adds `-fcrug-ffi` to that ADR's flag closure (`-fcrug-ffi` implies `-fcrug-ownership`); when the flag is implemented, ADR 0018 gains an amendment note so the closure table stays the single source of truth.

An explicitly named file that does not exist, does not parse, or names a function that is not declared in the translation unit is an error. A discovered file with an entry for an undeclared function is ignored, because one library-wide file legitimately covers many headers and translation units. In both cases a function that merely shares a name with an annotated one (different signature) is not annotated: matching is by identifier plus declared type, and a type mismatch is a conflict, not a silent skip.

`-fcrug-ffi` without a value is accepted as a deprecated spelling only if `-fcrug-ffi=<path>` is also present; it never means "discover everything".

### D6 -- Versioning

The header is mandatory and is the compatibility contract:

- `crugffi 1.0;` is the version this ADR defines.
- A file with major version other than 1 is rejected: `error: .crugffi major version 2 is newer than this compiler supports (1)`. There is no best-effort parsing of a newer major.
- A file with major 1 and a higher minor is parsed with the v1 grammar. Because v1 has no optional clauses, any clause this compiler does not know is an error regardless of minor, so a file that relies on a later minor's additions is rejected with a clear error instead of being half-understood and adding ownership the program does not have. Forward compatibility is standardised for nothing in v1: there are no ignorable extensions.
- A change that alters the meaning of an existing clause, that removes a clause, or that changes matching rules requires a new major version, a new ADR and a migration note in `docs/crug/FFI.md` when that file exists.
- The compiler reports its supported FFI version in `crugc --version` (ABI.md section 6) and defines `__CRUG_FFI_VERSION__` to the same number. The number is a compile-time fact; no run-time loader ever reads a sidecar.

### D7 -- No runtime, no ABI, no codegen

Metadata is consumed entirely at compile time and produces only diagnostics and analysis facts. Consequences, each of which is a test obligation:

- No symbol, section, table, constructor, relocation or dynamic dependency is added by the presence of a sidecar; `nm`/`readelf` output is byte-identical with and without `-fcrug-ffi` for code that is otherwise valid (Phase 9 gate, `diff-obj` check).
- Annotated and unannotated calls compile to the same machine code; the difference is what the front end accepts and what it drops.
- No sidecar is installed as a run-time requirement; an executable does not need the file to run.
- Sidecar parsing cannot execute code: no includes, no `eval`, no external commands, no file reads other than the files named by discovery.
- Resource bounds: a single sidecar is limited to 1 MiB and 10,000 declarations; exceeding either is an error, so a corrupt or hostile file cannot turn into unbounded work.

### D8 -- Trust model

A sidecar is **trusted data**, in the same sense as a header: it ships with the library, is installed by the same package, and is reviewed by whoever installs it. The compiler checks form, never behaviour.

What the compiler guarantees:

- A malformed file is rejected with a deterministic diagnostic; it cannot crash the compiler (bounded parser, bounded input, no execution).
- A file cannot change layout, ABI, codegen, symbol names or linkage (D7, ADR 0006).
- A file cannot create ownership where the header's types do not support it (type equality and pointer-level checks, D3).
- A file cannot silently contradict another file: conflicts are errors (D4).
- A file cannot be discovered by naming heuristics or by scanning: discovery is by header name, explicit flag, or configured directories only (D5). No inference from `_create`, `_free`, `_init` or any other spelling; RFC-0001 section 23 forbids exactly that, and it is the classic route to double frees.
- Under `-fcrug-strict`, only explicitly named files are honoured.

What the compiler cannot guarantee, and does not pretend to:

- That the callee actually behaves as stated. If a sidecar says `foo_create` returns owned and the real function returns an interior pointer, the resulting program can double-free. That is a library-contract violation, equivalent to a wrong declaration in a header, not a compiler bug. The mitigation is social and structural: the file ships with the library, is reviewable, and is versioned.
- That the destructor is complete. A destructor that frees half an object leaks; the compiler has no way to know.
- That nullability is obeyed. v1 records nullability and uses it where the object model allows (a null `own` binding is skipped by drop) but performs no nullness analysis.
- That the metadata file's provenance is authentic. v1 does not sign `.crugffi` files; package signatures are the distribution's job. A future ADR may require signed system metadata; that is an owner decision, not an implication of this format.

A sidecar is therefore a trust decision equivalent to installing the header itself, and the documentation must say so. It is not a security boundary against a malicious library, and treating it as one would be a category error.

### D9 -- Deliberately not in v1

These are named so that Phase 9 does not quietly grow scope, and so that shipped v1 files do not need rewriting when the later features arrive (state the clause, defer the enforcement):

- **Returned borrows** (`returns borrow_of argN`) and the general lifetime relations of RUST_MEMORY_GAPS section 26. Deferred to the focused FFI lifetime RFC (CRUG-RFC-0006 in the GAPS roadmap). v1 can state `retained ... until ...` but cannot check it; the format reserves the shape.
- **Conditional ownership** and success conditions: `out_owned` is only valid when the callee initialises on every normal return. `sqlite3_open` (which writes the handle even on some failures and not others) must be used at Level 1 with `adopt` in v1; the Phase 9 integration test therefore uses `adopt(sqlite3_open...)`-style handling and still proves `allocs=1 frees=1`. `realloc` is the same problem in argument position and is excluded from the libc example; a naive `consumes arg0 returns owned` entry for it would make correct error handling unexpressible.
- **Refcounted and retained-release APIs** (`retain`/`release` pairs, shared ownership) -- the ownership model has no shared owning type yet.
- **Enforcement** of `send`/`sync`/`!send`/`!sync`, `blocking` and `thread_affinity`; they are recorded for the concurrency phases (P17+) and no diagnostic depends on them in v1.
- **Struct members, macros, inline functions, function-like wrappers, variadic tails, C++ names, overloads**: a sidecar annotates external functions only.
- **A general binding generator**: no `.crugffi` is generated from headers in v1; a generator would need to be able to state lifetime facts this format cannot express yet.
- **A type transfer cast**: moving ownership from `own void *` to `own Foo *` is not expressible, so `malloc` metadata yields `own void *` bindings whose use is limited; typed allocation needs library-specific constructors. This is a known, documented limitation of the current ownership rules (ADR 0003/RFC-0001 section 14), not something the sidecar can patch.
- **A signature format** for the file (D8).

### D10 -- Worked example

`gcc/testsuite/crug.dg/ffi/libc.crugffi` is the format's reference example. It implements the small libc catalog of MASTER_SPEC section 16.5 minus `realloc` (D9):

```text
# Reference .crugffi: the small libc catalog of MASTER_SPEC 16.5, minus realloc
# (ADR 0007 D9: realloc needs conditional ownership, which v1 cannot express).
# This is a test fixture; it is not installed and is not discovered by default.

crugffi 1.0;

fn malloc
    returns owned void nullable drop free;

fn calloc
    returns owned void nullable drop free;

fn free
    consumes arg0 nullable;

fn fopen
    returns owned FILE nullable drop fclose;

fn fclose
    consumes arg0;
```

With this file explicitly supplied, `own void *p = malloc(16);` is a legal binding dropped by `free`, and `free(NULL)` is legal. Without the file, the same source needs `adopt(malloc(16), free)`. `own char *q = malloc(16);` is **not** legal: `void *` and `char *` are different types and a cast does not transfer ownership (D9). This is the honest cost of erasure, and the integration tests must use typed constructors from real libraries (`fopen`, `sqlite3` handles via `adopt`, `curl_easy_init`/`curl_easy_cleanup`) rather than pretending `malloc` yields a typed owned allocation.

### D11 -- Test obligations

The parser lands with these fixtures; each is a test, not documentation:

| Obligation | Fixture |
| --- | --- |
| The reference example parses and annotates | `gcc/testsuite/crug.dg/ffi/libc.crugffi` + a C driver that binds `own void *` and drops it once |
| Every clause kind parses and applies | `ffi/clauses.crugffi` + `ffi/clauses.c`, one function per clause |
| Missing or malformed header rejected | `ffi/no-version.crugffi`, `ffi/bad-syntax.crugffi` |
| Newer major rejected, unknown clause rejected | `ffi/version-2.crugffi`, `ffi/unknown-clause.crugffi` |
| Conflicts rejected deterministically | `ffi/conflict.crugffi` + `ffi/conflict.c`; run twice, stderr byte-identical, stable code |
| Out-of-range `argN`, variadic annotation, wrong destructor signature rejected | `ffi/bad-arg.crugffi`, `ffi/bad-destructor.crugffi` |
| Undeclared function: error when explicit, ignored when discovered | `ffi/unmatched.crugffi` + `ffi/unmatched.c` |
| Discovery: adjacent quoted include, system directory via `CRUG_FFI_PATH`, strict refuses discovery | `ffi/discovery/` |
| Metadata changes no code | `scripts/diff-obj.sh gcc/testsuite/crug.dg/ffi/ "-O2" "-O2 -fcrug-ffi=<fixture>"` => 0 differences |
| `out_owned` initialises a binding and drops it once | `ffi/out-owned.crugffi` + `ffi/out-owned.c` |
| `consumes` requires `move` and suppresses the caller's drop | `ffi/consumes.crugffi` + `ffi/consumes.c` |
| Level 2 for a real library, and the sqlite handle freed exactly once | Phase 9 `integration-sqlite` and the remaining integration tests |
| The FFI area's diagnostic codes are registered | `docs/crug/DIAGNOSTICS.md` (codes allocated with the parser; `scripts/check-diagnostics.sh` enforces the registry) |

`make check-FFI` (Phase 9) is the umbrella target; this ADR does not add it.

## Consequences

- Phase 9 has a decidable format: "finalise the .crugffi grammar v1" (its work item 1) is this document, and the parser has a closed set of clauses to implement.
- Ownership claims stay reviewable: a library's contract is one text file with a version number, diffable in a package, and a wrong claim is visible in review rather than hidden in compiler inference.
- The format is intentionally smaller than the union of every FFI feature in the master spec: lifetimes, refcounts, success conditions, struct members and generators are all deferred, with the clause shapes reserved so files do not churn.
- Strict mode gains a security-relevant meaning: metadata must be explicit, so a header cannot change a program's ownership by appearing in a search path.
- The libc catalog stays small and is a file, not hard-coded knowledge; the compiler has no special case for `malloc`.
- Cost: `out_owned` and `retained` are statements the compiler cannot verify, and v1 cannot express conditional ownership, so some real APIs (`sqlite3_open`, `realloc`) remain Level 1 until the lifetime RFC. That is a deliberate limitation, not an oversight.

## Alternatives rejected

- **A C-like or JSON/YAML syntax.** C-like invites preprocessor expectations and `#include`; JSON makes comments impossible and ownership files unreadable in review. The spec's own examples are line-oriented keyword clauses; v1 keeps that shape.
- **Attributes in the header (`__attribute__((crug_ffi(...)))`).** Requires editing or patching system headers, which RFC-0001 section 23 forbids, and would change header hashes.
- **Inference from names or from the binary.** RFC-0001 section 23 makes naming heuristics explicitly non-authoritative; inference would also make acceptance depend on undocumented compiler behaviour.
- **Last-file-wins overrides.** They make the effective contract depend on include order and hide mistakes; identical duplicates merge, differences are errors.
- **Optional clauses with an "ignore unknown" rule.** Ignoring a clause about destruction is exactly how a double free ships; v1 rejects unknown clauses and bumps the major version for meaning changes.
- **A run-time descriptor.** Forbidden by ADR 0003 D5 (no hidden state) and by the "no runtime" goal of the FFI layer.
- **Making the sidecar change codegen (e.g. emit destructor calls).** Destructor calls are generated from source-level ownership (ADR 0005), not from metadata; metadata only changes what the front end accepts and which drops it schedules for bindings the programmer wrote.

## Open questions

1. Should quoted-include discovery stay on by default, or should every sidecar be named? Current decision: quoted includes discover, angle includes never do, and strict mode requires explicit files.
2. Should a sidecar be allowed to annotate `static inline` wrappers (very common in real headers, e.g. `fopen` wrappers)? v1 says no; a wrapper can be given its own contract at Level 1 instead.
3. Is a 1 MiB / 10,000-declaration bound the right pair, and should it be an error or a warning at 90%?
4. Should the format grow a `send`/`sync` default for types not mentioned, so concurrency work does not need every type annotated before it can check anything?
5. Should `retained argN until F` require `F` to consume the same object (`consumes arg0` on `F`), so the end of the borrow is tied to a call the compiler can see?
6. Should the compiler warn when a discovered sidecar annotates a function the translation unit never calls (dead metadata), or is silence the right default?
7. Should system metadata be signed and verified (D8), and if so, by what key distribution?
8. Should `nullable` on a `returns owned` binding produce a compile-time obligation (a null check before first use) instead of only a documented contract? That would need a nullness analysis and is a candidate for the strict-mode default.
## Amendments

### A-1 (2026-10-19): the option spelling for discovery

D5's line "`-fcrug-ffi` without a value is accepted as a deprecated spelling only
if `-fcrug-ffi=<path>` is also present" cannot be implemented as written. The
option is declared as a joined option (`-fcrug-ffi=<path>`); a bare `-fcrug-ffi`
is rejected by the option parser before the front end sees it, with
"missing argument to '-fcrug-ffi='". Phase 9 therefore implements discovery
with a second option:

- `-fcrug-ffi=<path>` names one explicit sidecar (repeatable, command-line
  order); it does **not** turn discovery on.
- `-fcrug-ffi-discover` turns the layer on without naming a file, so D5's
  adjacency and system-directory steps run. It implies `-fcrug-ownership` and
  defines `__CRUG_FFI_VERSION__` exactly as `-fcrug-ffi=<path>` does.
- `-fcrug-strict` disables discovery (D5 unchanged), so a strict build names
  every contract explicitly.

D5 step 2 also needs one implementation note. libcpp's include callback receives
the directive name ("include"), not a directory, so the directory used for
adjacency is that of the file that contains the `#include` line, taken from the
line map. A header reached through `-I` therefore finds its adjacent sidecar
only when the sidecar sits next to the including file; otherwise the sidecar
must be named explicitly. `docs/crug/FFI.md` records this as a documented
limitation.

The flag closure note at the end of D5 is satisfied by both options
(`-fcrug-ffi=` and `-fcrug-ffi-discover` imply `-fcrug-ownership`).
