# The Crug language

Status: **Phase 23 implemented** (2026-10-21). This page is the map of the
language as the compiler implements it: the additions to C, the guarantee that
plain C keeps working, how a translation unit selects the features, what is
deliberately absent, and where the normative text lives. It is descriptive; if
it disagrees with an ADR, the ADR wins.

Crug is C with optional, separately gated checking. A translation unit that
gives no `-fcrug-*` option is compiled as plain C and is byte-identical to
what the GCC release it is built from would produce. A translation unit that
gives options opts into ownership, borrowing, tasks and channels, checked data
types and destructors, one or more features at a time. The language is defined
by the options it accepts and by [adr/0018-grammar.md](adr/0018-grammar.md),
not by a new `-std=` dialect.

## 1. What the language adds

Crug adds five groups of features. Each group is independent; a program may
use any subset, and the groups compose in one translation unit.

| Group | Options | Surface | Reference |
| --- | --- | --- | --- |
| Ownership and destruction | `-fcrug-ownership` | `own`, `raw`, `move`, `drop`, `adopt`, `release`, automatic destructors, `unsafe { }` | [OWNERSHIP.md](OWNERSHIP.md), [DROP.md](DROP.md), [STRICT.md](STRICT.md) |
| Borrowing | `-fcrug-borrow`, `-fcrug-alias-facts` | `ref`, `mut`, `borrow ()`, `borrow_mut ()`, conflict checking, last-use lifetimes | [BORROW.md](BORROW.md), [BORROWING.md](BORROWING.md) |
| Tasks and channels | `-fcrug-goroutines`, `-fcrug-channels` | `go`, `yield`, `scope`, `chan T`, `channel`/`send`/`recv`/`close`, `select` | [GOROUTINES.md](GOROUTINES.md), [SCOPE.md](SCOPE.md), [CHANNELS.md](CHANNELS.md), [TASKS.md](TASKS.md) |
| Checked data types | `-fcrug-slices`, `-fcrug-result`, `-fcrug-adt` | `slice<T>`, `option<T>`, `result<T, E>`, `some`/`none`/`ok`/`err`, payload enums, `match` | [SAFE-TYPES.md](SAFE-TYPES.md) |
| Checking policy and the C boundary | `-fcrug-strict`, `-fcrug-ffi=` | `unsafe`, `raw`, `.crugffi` contracts for calls into C libraries | [STRICT.md](STRICT.md), [FFI.md](FFI.md) |

A small program using ownership and borrowing:

```c
void fill (mut char *buf, int n)              /* a mutable borrow parameter */
{
  for (int i = 0; i < n; i++)
    buf[i] = 'x';                             /* writes through the borrow */
}

void run (void)
{
  own char *p = malloc (16);                  /* owned by free (), catalogue */
  fill (borrow_mut (p), 16);                  /* a checked mutable borrow */
  drop (p);                                   /* destroyed here; borrow is dead */
}
```

Three properties hold for every group:

- **No new keywords.** Every Crug word is a contextual identifier. `own`,
  `go`, `chan`, `select`, `move` and the rest remain usable as ordinary C
  names; a declaration in scope wins over the Crug interpretation, and a macro
  wins over both ([adr/0002-contextual-keywords.md](adr/0002-contextual-keywords.md),
  [adr/0018-grammar.md](adr/0018-grammar.md) D1, D3, D9).
- **Static and erased.** Ownership and borrow facts live in the frontend; no
  hidden field, side table, reference count or helper call is added, and the
  qualifiers are erased at type boundaries, so layout and calling conventions
  match plain C ([adr/0003-ownership-metadata-storage.md](adr/0003-ownership-metadata-storage.md),
  [ABI.md](ABI.md)). The concurrency features are the one part with a runtime:
  the `libcrug` task and channel runtime ([TASKS.md](TASKS.md),
  [CHANNELS.md](CHANNELS.md)).
- **Diagnosed by code.** Every rejection carries a stable
  `crug-<AREA>-<NNN>` code; every warning has a `-Wcrug-*` option, and
  `-fcrug-strict` promotes exactly the warnings listed in
  [DIAGNOSTICS.md](DIAGNOSTICS.md).

The grammar is described in [GRAMMAR.md](GRAMMAR.md). Ownership semantics are
in [OWNERSHIP.md](OWNERSHIP.md) and [DROP.md](DROP.md); borrowing in
[BORROW.md](BORROW.md); the unsafe escape hatch in [STRICT.md](STRICT.md) and
[UNSAFE.md](UNSAFE.md).

## 2. Plain C stays plain C

The central compatibility claim is simple: with no `-fcrug-*` option,
`gcc/crugc` accepts the same C programs as the GCC driver it is built from
and produces the same object code. The claim is checked, not asserted.

| Evidence | What it shows |
| --- | --- |
| `scripts/diff-cc.sh corpus/` | 30 programs compiled with `crugc` and the baseline `xgcc`; exit status and object bytes compared |
| `make check-C_COMPAT` | the plain-C corpus at `-O0`, `-O2` and `-Os` ("3 option matrices plus 2 checks, 0 FAIL") |
| `scripts/check-crug-flags.sh` | `-dM -E` predefined-macro output compared with the baseline (149 work items) |
| `gcc/testsuite/crug.dg/flags/words-as-identifiers.c` | every contextual word declared and used as a C identifier |
| `bench/run.py --pristine --check` | 114 source/option pairs at `-O0`, `-O2` and `-O3` byte-identical to unmodified GCC 16.2 ([PERFORMANCE.md](PERFORMANCE.md) section 3.1) |
| phase 0 baseline | 224,481 passing testsuite entries recorded in `docs/crug/baseline/phase0/` |

The mechanisms that keep this true are structural, not promises: the parser
adds no preprocessing token and every Crug word is still a `CPP_NAME`; no
macro is defined unless at least one `-fcrug-*` option is active
([FLAGS.md](FLAGS.md) section 3); a user declaration named `move`, `go`,
`chan` or `select` wins over the Crug meaning; and where a construct could be
read two ways the C parse is tried first and wins where it succeeds
([adr/0018-grammar.md](adr/0018-grammar.md) D1, D6, D7).

Plain C and checked C coexist in one translation unit. Only declarations that
carry a Crug qualifier, and calls to the intrinsics, are analysed; unannotated
functions, structs and pointers are compiled as C. A C function can be called
with an owned value only through `move`, `release` or a `.crugffi` contract
([FFI.md](FFI.md)); without any of those the call is a diagnostic under
`-fcrug-strict` and plain C elsewhere.

`gcc/crugc` is a launcher, not a second compiler: it locates the GCC driver
built from this tree and appends the caller's arguments unchanged
([DRIVER.md](DRIVER.md)). It adds no includes, no library and no default
options. There is no `libcrug` dependency for ownership, borrow, strict mode or
FFI; only programs that use tasks or channels link the runtime.

## 3. Feature gates and dialect selection

There is no `-std=crug`. The dialect is the set of `-fcrug-*` options given
on the command line, and every option is usable alone. The option table is
normative in [FLAGS.md](FLAGS.md) section 1 and `gcc/common.opt`; this is the
summary.

| Option | Enables | Implies |
| --- | --- | --- |
| `-fcrug-ownership` | `own`, `raw`, `move`, `drop`, `adopt`, `release`, destructors, `unsafe` | - |
| `-fcrug-borrow` | `ref`, `mut`, `borrow`, `borrow_mut`, borrow checking | ownership |
| `-fcrug-alias-facts` | `mut` and `own` parameters become `restrict` facts for the optimizer | borrow (and ownership) |
| `-fcrug-goroutines` | `go`, `yield`, `scope` | - |
| `-fcrug-channels` | `chan T`, `channel`, `send`, `recv`, `close`, `select` | goroutines |
| `-fcrug-slices` | `slice<T>` and checked indexing | - |
| `-fcrug-result` | `option<T>`, `result<T, E>`, `some`, `none`, `ok`, `err` | adt |
| `-fcrug-adt` | payload enums and `match` | - |
| `-fcrug-strict` | the strict checking policy (no production of its own) | ownership, borrow |
| `-fcrug-ffi=<file>` | one `.crugffi` contract file, repeatable | ownership |
| `-fcrug-ffi-discover` | discovers contracts for quoted includes | ownership |
| `-fcrug-modern` | ownership, borrow, slices, result, adt, goroutines, channels, strict | all of those |
| `-fcrug-kernel` | kernel mode marker; removes `go`, `scope`, `chan`, `select` | - |
| `-fcrug-bounds-checks` / `-fno-crug-bounds-checks` | slice bounds checking; on by default | - |
| `-fcrug-dump=ownership,drop,borrow,alias` | a dump of the named analysis, next to the object file | - |

The options are closed once per compilation, after the command line has been
read. Closure only adds features: `-fcrug-borrow` turns ownership on,
`-fcrug-channels` turns goroutines on, and `-fcrug-strict` turns ownership
and borrowing on. Giving a feature without its prerequisite is legal and not a
diagnostic; the prerequisite is simply enabled
(gcc/c-family/crug-common.cc, ADR 0018
D1). `-fcrug-modern` is defined by the closure, not by the option table, so it
has exactly one meaning; it deliberately does not include `-fcrug-alias-facts`
(a code-generation fact is opt-in) and does not include kernel mode.

Kernel mode is a mode marker, not a second dialect. In this release
`-fcrug-kernel` combined with `-fcrug-goroutines`, `-fcrug-channels` or
`-fcrug-modern` is a hard error, and so is combining it with ownership or
borrowing; the message records that kernel-mode destruction lowering is a
non-goal of this release (ADR 0017). The v1 kernel story is plain GNU C, recorded as
proposed in [adr/0017-kernel-ownership-policy.md](adr/0017-kernel-ownership-policy.md)
and measured in [KERNEL-COMPATIBILITY.md](KERNEL-COMPATIBILITY.md).

A feature is visible at compile time through the macros in [FLAGS.md](FLAGS.md)
section 3: `__CRUG__` and `__CRUG_COMPILER__` when any `-fcrug-*` option is
active, then one macro per active feature (`__CRUG_OWNERSHIP__`,
`__CRUG_BORROW__`, `__CRUG_ALIAS_FACTS__`, `__CRUG_SLICES__`,
`__CRUG_RESULT__`, `__CRUG_ADT__`, `__CRUG_GOROUTINES__`,
`__CRUG_CHANNELS__`, `__CRUG_STRICT__`, `__CRUG_KERNEL__`), plus
`__CRUG_FFI_VERSION__` (`100`) when an FFI option is active. The macros exist
only in Crug mode, so an unmodified C build never sees them.

Recommended sets, from the smallest upwards:

- `-fcrug-ownership`: destructors and `move`, no borrow checking.
- `-fcrug-modern`: the normal full-language set for new code.
- `-fcrug-modern -fcrug-strict`: adds the strict call and unsafe policy.
- `-fcrug-ffi=libc.crugffi -fcrug-strict`: checked calls into a C library.

Versioning of the feature set is a compatibility-policy question, not a
`-std=` question: [COMPATIBILITY-POLICY.md](COMPATIBILITY-POLICY.md) section 1
says how features are added or deprecated.

## 4. What is not in the language

Several things a reader might expect from a language with ownership are
deliberately absent. This section is a list of non-features, so that a missing
page or option is not mistaken for an undocumented one.

- **No new keywords.** Crug words are contextual; see section 1 and
  [GRAMMAR.md](GRAMMAR.md) section 1. There is no way to reserve them.
- **No user-defined generic types.** `option`, `result` and `slice` are the
  only parameterised types, and their arguments are checked by the compiler.
  Generics stabilization is post-1.0 and is not implemented; `vector<int>`
  where `vector` is a typedef is a diagnostic, not an instantiation.
- **No traits or interfaces.** There are no user-declared abstractions over
  types; the compiler special-cases the standard payload and container types.
- **No shared ownership.** Crug does not provide reference counting, `Rc`-like
  handles, a garbage collector or aliased ownership; an owned value has exactly
  one owner at a time, and shared ownership is not implemented.
- **No exceptions and no unwinding.** Destruction is lexical, tied to scope
  exits, `return`, `break`, `continue` and `goto` out of a scope; there is
  no exception object to catch, and no destructor runs for `exit`, `abort` or
  a signal ([DROP.md](DROP.md) section 5).
- **No implicit ownership transfer and no copy.** An owned value cannot be
  copied or assigned. Under `-fcrug-strict` it also cannot be passed to an
  `own` parameter without `move` (`crug-OWN-011`); hybrid mode accepts the
  pass as a migration path and does not track the hand-off at the call site,
  because an `own` parameter is erased at the boundary. A cast cannot create
  ownership ([adr/0018-grammar.md](adr/0018-grammar.md) D2 R3).
- **No whole-program borrow inference.** Borrows cross a call only as a
  `ref` or `mut` argument, cannot be returned or stored in memory, and are
  not summarized across functions; member and array-element borrows and partial
  moves are refused in this phase ([adr/0019-interprocedural-borrow-policy.md](adr/0019-interprocedural-borrow-policy.md),
  [adr/0020-last-use-liveness.md](adr/0020-last-use-liveness.md)).
- **No memory-safety guarantee.** `unsafe` and `raw` exist because some code
  must do unchecked work; the checker reduces mistakes, it does not make C
  memory-safe ([UNSAFE.md](UNSAFE.md) section 6).
- **No kernel-mode ownership or borrowing.** `-fcrug-kernel` rejects both
  options in this release; kernel sources compile as plain GNU C
  ([adr/0017-kernel-ownership-policy.md](adr/0017-kernel-ownership-policy.md)).
- **No separate ABI, runtime or object model.** Crug is compiled by a GCC
  driver and links like C; only tasks and channels use the runtime
  ([ABI.md](ABI.md), [DRIVER.md](DRIVER.md)).

## 5. Canonical references

When this page and a normative document disagree, the normative document wins.

| Topic | Canonical text |
| --- | --- |
| Contextual recognition and collisions | [adr/0002-contextual-keywords.md](adr/0002-contextual-keywords.md) |
| Ownership metadata, erasure and forbidden representations | [adr/0003-ownership-metadata-storage.md](adr/0003-ownership-metadata-storage.md) |
| Ownership analysis, aggregates, drop order, unions, adoption | [adr/0004-ownership-analysis-layer.md](adr/0004-ownership-analysis-layer.md) |
| Where destruction happens | [adr/0005-drop-lowering.md](adr/0005-drop-lowering.md) |
| Grammar, qualifier attachment, statements, payload enums | [adr/0018-grammar.md](adr/0018-grammar.md); productions in `spec/RFC-0001` sections 13-25 (0.2-draft) |
| Borrows across calls (depth zero) | [adr/0019-interprocedural-borrow-policy.md](adr/0019-interprocedural-borrow-policy.md) |
| Last-use lifetimes | [adr/0020-last-use-liveness.md](adr/0020-last-use-liveness.md) |
| Strict mode, `raw` and `unsafe` | [adr/0021-strict-mode-policy.md](adr/0021-strict-mode-policy.md) |
| C ABI policy | [adr/0006-c-abi-policy.md](adr/0006-c-abi-policy.md) |
| `.crugffi` format | [adr/0007-ffi-metadata-format.md](adr/0007-ffi-metadata-format.md) |
| Tasks, scopes and channels | [adr/0024-structured-scope.md](adr/0024-structured-scope.md), [adr/0025-channels-and-select.md](adr/0025-channels-and-select.md), [adr/0026-send-sync.md](adr/0026-send-sync.md) |
| Feature options and macros | [FLAGS.md](FLAGS.md) |
| Diagnostic codes and severities | [DIAGNOSTICS.md](DIAGNOSTICS.md) |
| Phase gates and archived evidence | [EXIT-GATES.md](EXIT-GATES.md) (phases 1, 3, 4, 5, 6, 7, 8, 9, 14-23) |
| Project status | [progress.md](progress.md) |
