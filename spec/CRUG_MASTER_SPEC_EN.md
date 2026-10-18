# CRUG — Master Language, Compiler, Runtime, Portability, Kernel-Compatibility, and AI-Harness Implementation Specification

**Status:** Working master specification  
**Language name:** Crug  
**Compiler base:** GCC 16.2 baseline fork  
**Primary implementation strategy:** Extend the GCC C frontend with opt-in Crug semantics, lower to standard GCC IR, preserve the C ABI, and keep runtime features modular  
**Primary implementation targets:** Linux x86-64 first, then AArch64; architecture must be portable by design to Windows, macOS, BSD, Android, bare metal, and other GCC-supported targets  
**Kernel constraint:** **Compile existing Linux kernel C semantics without runtime overhead**  
**Core philosophy:** C when you want it. Rust-like safety when you ask for it. Go-like concurrency when you need it. Portable by design.

---

# 0. Executive Summary

Crug is a systems language built as an extension of C rather than as a replacement for C.

Its design combines:

- the syntax, ABI, ecosystem, predictability, and low-level control of C;
- optional Rust-inspired ownership, borrowing, move semantics, RAII-like destruction, safer slices, `Option`, `Result`, algebraic data types, and compile-time concurrency safety;
- optional Go-inspired lightweight tasks, channels, `select`, and structured concurrency;
- GCC as the compiler foundation, so Crug can reuse GCC's C parser, GNU C compatibility, optimizer, GIMPLE/SSA middle end, RTL backend infrastructure, target support, diagnostics infrastructure, LTO machinery, and test framework.

Crug must support several distinct usage profiles:

```text
1. Plain C compatibility
   crugc file.c

2. C + ownership
   crugc -fcrug-ownership file.c

3. C + ownership + borrowing
   crugc -fcrug-ownership -fcrug-borrow file.c

4. C + Go-like concurrency
   crugc -fcrug-goroutines file.c

5. Modern Crug
   crugc -fcrug-modern file.c

6. Freestanding / kernel-compatible C
   crugc -ffreestanding -fcrug-kernel file.c
```

The fundamental invariant is:

> If Crug extensions are disabled, the compiler must behave as a C compiler, not as a hidden managed-language compiler.

The additional kernel invariant is:

> **Existing Linux kernel C semantics must compile without mandatory Crug runtime support or hidden runtime overhead.**

---

# 1. Mandatory Instructions for the AI Development Harness

This document is both a language specification and an execution plan.

The AI harness must treat the following rules as mandatory unless a later Architecture Decision Record explicitly replaces one.

## 1.1 General Rules

1. Never break ordinary C semantics when Crug extensions are disabled.
2. `crugc file.c` must remain a C compilation mode.
3. Existing C headers, static libraries, shared libraries, calling conventions, and data layouts must remain usable.
4. `own`, `ref`, `mut`, `move`, borrow checking, and Send/Sync-like checking must be compile-time abstractions unless the language feature explicitly requires runtime support.
5. Do not add a garbage collector.
6. Do not add hidden reference counting to `own`, `ref`, or `mut`.
7. Do not add hidden mutexes for `mut`.
8. Do not add hidden heap allocations for `move` or `borrow`.
9. Never add hidden ownership fields, reference counts, type descriptors, or lifetime fields to ordinary C structs.
10. Prefer standard GCC GENERIC/GIMPLE lowering instead of inventing Crug-specific middle-end IR opcodes.
11. New GIMPLE codes require an ADR proving standard lowering is insufficient.
12. Preserve source locations through semantic analysis and lowering.
13. Every language feature must have positive tests, negative tests, diagnostic tests, ABI tests where relevant, and regression tests.
14. Every compiler bug fix must add a regression test.
15. Every runtime bug fix must add a stress or regression test.
16. Every optimization must have correctness tests and performance evidence.
17. Never trade semantic correctness for benchmark results.
18. Keep patches small, reviewable, and reversible.
19. Do not perform broad GCC frontend refactors merely to make Crug code aesthetically cleaner.
20. Search GCC for the closest existing implementation pattern before adding new infrastructure.
21. Generated files must not be edited when a generator source exists.
22. Feature flags must be independently controllable unless a dependency is explicitly specified.
23. Diagnostics are part of the product, not an afterthought.
24. Unknown FFI ownership is unmanaged by default.
25. AI guesses are never equivalent to static proof.

## 1.2 Portability Rules

1. `libcrug/core` must contain no direct operating-system API calls.
2. All operating-system interaction must go through the Crug PAL: **Platform Abstraction Layer**.
3. All CPU/ABI-specific context-switch logic must live under `libcrug/arch`.
4. A new OS port must not require semantic changes to the core scheduler.
5. A new CPU port must not require OS-specific changes to the language frontend.
6. Crug ownership features must remain usable on targets that do not support goroutines.
7. A bare-metal target may support ownership while rejecting `go`, channels, or other unavailable runtime features.

## 1.3 Linux Kernel Compatibility Rules

The following constraint is permanent:

> **Compile existing Linux kernel C semantics without runtime overhead.**

The AI harness must interpret this as all of the following:

1. Plain kernel C must not require `libcrug`.
2. Plain kernel C must not add Crug startup code.
3. Plain kernel C must not add TLS merely because Crug is the compiler.
4. Plain kernel C must not add hidden heap allocations.
5. Plain kernel C must not add hidden locks.
6. Plain kernel C must not add hidden bounds checks.
7. Plain kernel C must not add task/scheduler structures.
8. Plain kernel C must not add constructors/destructors unrelated to explicit source semantics.
9. GNU C extensions used by the Linux kernel must continue to parse and preserve their GCC semantics.
10. Inline assembly, `asm goto`, volatile accesses, compiler barriers, section annotations, symbol attributes, packed layouts, and architecture-specific builtins must remain usable.
11. `-ffreestanding`, `-nostdlib`, `-nostartfiles`, kernel linker scripts, and Kbuild-style compilation must remain possible.
12. When no Crug language feature is used, the generated program must not contain Crug runtime symbols.
13. Crug compile-time ownership may eventually be usable in kernel code only when it lowers to ordinary kernel-compatible operations and does not require `libcrug`.
14. Go-like goroutines are **not** automatically part of kernel compatibility. A future kernel scheduler backend would be a separate feature and must never be silently enabled.
15. Kernel compatibility tests must be first-class CI tests, not manual experiments.

## 1.4 Required AI Harness Work Loop

For each task:

1. Read this specification.
2. Read `docs/crug/progress.md`.
3. Read all ADRs relevant to the subsystem.
4. Identify the exact GCC components involved.
5. Search existing GCC implementation patterns.
6. Add or select the smallest test expressing the desired behavior.
7. Implement the smallest correct patch.
8. Build the affected compiler component.
9. Run targeted Crug tests.
10. Run C regression tests.
11. If kernel semantics may be affected, run kernel-compatibility tests.
12. If runtime code changed, run ASan/UBSan and relevant stress tests.
13. If code generation changed, inspect GIMPLE and assembly.
14. If optimization changed, compare `-O0`, `-O2`, and `-O3`.
15. Update `docs/crug/progress.md`.
16. Report:
    - files changed;
    - semantics added or changed;
    - tests run;
    - failures;
    - known limitations;
    - next task.
17. Do not mark a phase complete until its exit criteria pass.

## 1.5 Recommended Commit Format

```text
crug: driver: add kernel compatibility profile
crug: parser: recognize contextual own qualifier
crug: sema: reject use after move
crug: borrow: add exclusive mutable borrow checking
crug: drop: insert cleanup on normal scope exit
crug: ffi: load sidecar ownership metadata
crug: kernel: preserve GNU C statement expression semantics
libcrug: pal: add Linux thread backend
libcrug: task: add cooperative scheduler
testsuite: crug: add kernel no-runtime regression tests
```

---

# 2. Project Vision

Crug combines three established systems-programming models without requiring users to adopt all three.

```text
                         CRUG
                           |
        +------------------+------------------+
        |                  |                  |
        C                 Rust                Go
        |                  |                  |
      syntax             safety          concurrency
       ABI              ownership         goroutines
    ecosystem           borrowing         channels
   low-level            move/drop          select
   freestanding         Result/ADT          scope
        |                  |                  |
        +------------------+------------------+
                           |
                       GCC native code
```

Crug is not intended to hide C.

Crug is intended to let a codebase move along a continuum:

```text
legacy C
   |
   +--> plain Crug compiler, no semantic changes
   |
   +--> optional ownership
   |
   +--> optional borrowing
   |
   +--> safe slices / Result / ADTs
   |
   +--> structured concurrency
```

A very large C codebase must be able to migrate incrementally.

---

# 3. Project Goals

## 3.1 Primary Goals

Crug should provide:

- strong compatibility with C source semantics;
- strong compatibility with GNU C where GCC already supports it;
- C ABI compatibility;
- existing C header compatibility;
- existing `.a` / `.so` compatibility;
- optional ownership;
- optional borrowing;
- move semantics;
- deterministic destruction of owned resources;
- explicit unsafe/raw boundaries;
- safer slices;
- optional `Option<T>`;
- optional `Result<T,E>`;
- optional algebraic data types;
- optional exhaustive pattern matching;
- optional generic programming;
- optional lightweight tasks;
- optional typed channels;
- optional `select`;
- structured concurrency;
- Send/Sync-like static checks;
- no mandatory GC;
- no mandatory runtime for compile-time-only features;
- multi-OS portability;
- freestanding support;
- Linux kernel C compatibility without runtime overhead;
- optimization opportunities from stronger alias/lifetime information.

## 3.2 Non-Goals for Early Releases

Do not initially attempt to provide:

- Rust source compatibility;
- Go source compatibility;
- Rust procedural macros;
- the full Rust trait system;
- C++ classes or exceptions;
- mandatory reflection metadata;
- mandatory GC;
- a JIT;
- a preemptive scheduler in the first task-runtime version;
- automatically safe conversion of arbitrary historical C;
- automatic goroutine conversion of complex `pthread` programs;
- full kernel-safe goroutines;
- universal source compatibility with every historical compiler extension outside GCC's supported C dialects.

---

# 4. Definition of Success

Crug 1.0 is successful when:

1. Plain C mode passes the selected GCC C regression corpus.
2. Existing Linux-kernel-style C constructs compile with preserved semantics.
3. Kernel/freestanding builds do not link `libcrug` when no runtime feature is used.
4. Kernel/plain-C object files contain no unexpected `__crug_*` references.
5. `own` and `move` correctly detect invalid ownership usage.
6. `ref` and `mut` correctly enforce the selected borrow model.
7. Owned resources are destroyed exactly once on supported control-flow paths.
8. C libraries can be linked without recompilation.
9. C struct and function ABI remains unchanged where Crug qualifiers are ABI-transparent.
10. Goroutines use a bounded worker-thread model rather than one thread per task.
11. Channels can transfer owned values.
12. Structured scopes bound child-task lifetimes.
13. The compiler can exploit at least some proven Crug aliasing guarantees.
14. Compile-time-only Crug features do not introduce runtime allocation/refcount/locking overhead.
15. Diagnostics identify both the invalid use and the ownership/borrow origin.
16. Ports to at least two distinct OS families prove the PAL architecture.
17. At least one freestanding/kernel-style target proves runtime independence.

---

# 5. GCC Foundation

## 5.1 Baseline Strategy

Start from a fixed GCC 16.2 baseline.

Create a stable fork branch:

```text
crug/gcc-16.2
```

Avoid continuous rebasing onto GCC development branches during early implementation.

Compiler rebases are explicit projects with full regression runs.

## 5.2 Integration Philosophy

Use the GCC C frontend as the compatibility anchor.

Target pipeline:

```text
C / Crug source
      |
      v
GCC preprocessor / libcpp
      |
      v
GCC C parser + minimal Crug hooks
      |
      +--> standard C semantics
      |
      +--> Crug semantic layer
             |
             +-- ownership
             +-- borrowing
             +-- lifetime checks
             +-- drop elaboration
             +-- FFI metadata
             +-- concurrency safety
      |
      v
Crug lowering
      |
      v
GENERIC
      |
      v
GIMPLE / SSA
      |
      v
GCC optimization pipeline
      |
      v
RTL
      |
      v
target backend
```

## 5.3 Main GCC Areas to Inspect

Verify exact paths in the selected GCC baseline before modifying them.

Likely areas:

```text
gcc/c/c-parser.cc
gcc/c/c-decl.cc
gcc/c/c-typeck.cc
gcc/c/c-tree.h
gcc/c-family/
gcc/c-family/c.opt
gcc/gimple*
gcc/tree-ssa*
gcc/alias.cc
gcc/tree-ssa-alias*
gcc/tree-vrp*
gcc/testsuite/gcc.dg/
gcc/testsuite/gcc.target/
libgomp/
libgo/
```

Create Crug-specific implementation areas progressively:

```text
gcc/crug/
    crug.h
    crug-flags.h
    crug-types.cc
    crug-sema.cc
    crug-ownership.cc
    crug-borrow.cc
    crug-lifetime.cc
    crug-drop.cc
    crug-ffi.cc
    crug-kernel.cc
    crug-lower.cc
    crug-diagnostics.cc
    crug-dump.cc

gcc/testsuite/crug.dg/
```

Avoid copying the entire C frontend unless a later architectural checkpoint proves a separate frontend is necessary.

---

# 6. Compiler Driver and Profiles

## 6.1 Driver

The user-facing compiler executable is:

```bash
crugc
```

An initial wrapper around GCC's driver is acceptable.

The long-term implementation should integrate cleanly with GCC driver specifications.

## 6.2 Core Feature Flags

Canonical internal options:

```bash
-fcrug-ownership
-fcrug-borrow
-fcrug-slices
-fcrug-result
-fcrug-adt
-fcrug-goroutines
-fcrug-channels
-fcrug-strict
-fcrug-kernel
-fcrug-modern
```

Convenience aliases may exist:

```bash
-fownership
-fgoroutines
-fmodern
```

but internal options should remain namespaced.

## 6.3 Profiles

### Plain C

```bash
crugc file.c
```

Properties:

- C semantics;
- no Crug ownership checking;
- no goroutine runtime;
- no `libcrug`;
- contextual Crug words behave as ordinary identifiers unless unambiguously enabled by a specific feature.

### Ownership

```bash
crugc -fcrug-ownership file.c
```

Enables:

- `own`;
- `move`;
- automatic drop where specified.

### Safe ownership

```bash
crugc -fcrug-ownership -fcrug-borrow file.c
```

Adds:

- `ref`;
- `mut`;
- borrow checking.

### Goroutines

```bash
crugc -fcrug-goroutines file.c
```

Adds:

- `go`;
- task runtime linkage when used.

### Modern Crug

```bash
crugc -fcrug-modern file.c
```

May expand to:

```text
ownership
borrow
slices
Result
ADT
goroutines
channels
strict diagnostics
```

The exact feature set must be versioned and documented.

### Kernel/Freestanding Compatibility

```bash
crugc -fcrug-kernel -ffreestanding ...
```

Properties:

- prioritize GNU C / kernel semantics;
- no automatic runtime linkage;
- no scheduler;
- no hidden heap allocation;
- no hidden TLS;
- no runtime bounds machinery;
- compile-time ownership features only when explicitly enabled;
- `go`/channels unavailable unless a future explicit kernel runtime backend exists.

## 6.4 Feature Dependencies

```text
-fcrug-borrow
    requires ownership

-fcrug-channels
    requires goroutine/task runtime

-fcrug-strict
    requires ownership + borrow

-fcrug-kernel
    does NOT imply ownership
    does NOT imply goroutines
    implies runtime-free plain-C compatibility policy
```

---

# 7. Contextual Lexical Model

## 7.1 Contextual Words

The following should not become globally reserved words in plain C:

```text
own
ref
mut
raw
move
borrow
borrow_mut
unsafe
drop
go
chan
select
scope
match
some
none
ok
err
shared
```

For example:

```c
int own = 42;
int go = 7;
```

must remain legal C.

## 7.2 Parser Principle

Prefer parser-context recognition:

```text
identifier token
      |
      v
parser context
      |
      +-- declaration qualifier position + ownership enabled
      |      "own" => CRUG ownership qualifier
      |
      +-- statement position + goroutines enabled
      |      "go" => Crug go statement
      |
      +-- otherwise
             ordinary C identifier
```

This is critical for compatibility with existing C codebases, including the Linux kernel.

---

# 8. Crug Grammar

The grammar below is normative in intent but may be adjusted to fit GCC frontend structures.

## 8.1 Ownership Qualifiers

```ebnf
crug-ownership-qualifier
    ::= "own"
     |  "ref"
     |  "mut"
     |  "raw"
```

Examples:

```c
own Foo *a;
ref Foo *b;
mut Foo *c;
raw Foo *d;
```

Meaning:

```text
own T*  unique owning pointer
ref T*  shared read borrow
mut T*  exclusive mutable borrow
raw T*  explicitly unchecked pointer in strict contexts
T*      ordinary C pointer in hybrid mode
```

## 8.2 Move

```ebnf
move-expression
    ::= "move" "(" assignment-expression ")"
```

Example:

```c
own Foo *a = foo_create();
own Foo *b = move(a);
```

State transition:

```text
a: OWNED -> MOVED
b: OWNED
```

## 8.3 Borrow

```ebnf
borrow-expression
    ::= "borrow" "(" assignment-expression ")"
     |  "borrow_mut" "(" assignment-expression ")"
```

Examples:

```c
ref Foo *r = borrow(owner);
mut Foo *m = borrow_mut(owner);
```

## 8.4 Unsafe

```ebnf
unsafe-statement
    ::= "unsafe" compound-statement
```

Example:

```c
unsafe {
    raw volatile uint32_t *reg =
        (raw volatile uint32_t *)0x40000000;
    *reg = 1;
}
```

`unsafe` itself must have no runtime cost.

## 8.5 Go Statement

MVP:

```ebnf
go-statement
    ::= "go" postfix-expression "(" argument-expression-list? ")" ";"
```

Example:

```c
go worker(42);
go worker(move(job));
```

Do not initially support arbitrary expressions after `go`.

## 8.6 Structured Scope

```ebnf
scope-statement
    ::= "scope" compound-statement
```

Example:

```c
scope {
    go worker1();
    go worker2();
}
```

Leaving the scope waits for its child tasks.

## 8.7 Channels

Target syntax:

```ebnf
channel-type
    ::= "chan" type-name
```

Examples:

```c
chan int jobs;
chan own Job *jobs;
```

Creation and operations:

```c
chan int jobs = channel(128);

send(jobs, 42);
int x = recv(jobs);
close(jobs);
```

## 8.8 Select

Later target syntax:

```c
select {
case value = recv(input):
    process(value);

case send(output, value):
    sent++;

default:
    yield();
}
```

## 8.9 Option and Result

Target forms:

```c
option<int>
option<own Foo *>

result<int, Error>
result<own Foo *, Error>
```

## 8.10 Pattern Matching

Target:

```c
match (r) {
    ok(v)  => use(v),
    err(e) => report(e),
}
```

Exhaustiveness should be checked for Crug algebraic data types.

---

# 9. Type Model and ABI

## 9.1 ABI Transparency of Pointer Qualifiers

`own`, `ref`, `mut`, and `raw` must not change pointer representation.

On a target with 8-byte pointers:

```text
sizeof(own Foo*) == 8
sizeof(ref Foo*) == 8
sizeof(mut Foo*) == 8
sizeof(raw Foo*) == 8
```

## 9.2 Struct Layout

This:

```c
struct Player {
    own char *name;
    int score;
};
```

must have the same physical layout as:

```c
struct Player {
    char *name;
    int score;
};
```

unless a specifically Crug-native non-C-ABI type is used.

## 9.3 Metadata Storage

Prefer Crug side tables or frontend annotations rather than modifying fundamental GCC `tree` representation globally.

Conceptual metadata:

```cpp
enum crug_ownership_kind {
    CRUG_UNMANAGED,
    CRUG_OWNED,
    CRUG_SHARED_BORROW,
    CRUG_MUT_BORROW,
    CRUG_RAW
};

struct crug_type_info {
    crug_ownership_kind ownership;
    tree destructor;
    bool sendable;
    bool syncable;
};
```

The exact structure is implementation-specific.

---

# 10. Ownership Semantic Model

## 10.1 Minimum States

Track at least:

```text
UNINITIALIZED
OWNED
MOVED
DROPPED
MAYBE_INITIALIZED
MAYBE_MOVED
```

## 10.2 Basic Transition

```text
UNINITIALIZED
     |
 assignment
     v
OWNED
     |
 move
     v
MOVED
```

Drop:

```text
OWNED -> DROPPED
```

Use in `MOVED`, `DROPPED`, or uninitialized state is an error.

## 10.3 No Implicit Copy of Owned Values

Invalid:

```c
own Foo *a = foo_create();
own Foo *b = a;
```

Expected diagnostic:

```text
error: copy of owned value 'a' is not allowed
help: use move(a) to transfer ownership
```

Valid:

```c
own Foo *b = move(a);
```

## 10.4 Function Parameters

Examples:

```c
void consume(own Foo *x);
void inspect(ref Foo *x);
void update(mut Foo *x);
void legacy(Foo *x);
```

Calls:

```c
consume(move(foo));
inspect(borrow(foo));
update(borrow_mut(foo));
legacy(foo);
```

Strict mode should require an explicit unsafe/raw boundary when an owned value is passed into unmanaged semantics in a way that may violate ownership.

## 10.5 Returns

```c
own Foo *create(void);
```

transfers ownership to the caller.

```c
ref Foo *lookup(ref Container *c);
```

ties the returned borrow to a valid source lifetime.

---

# 11. Borrow Checker

## 11.1 Core Rule

For one logical memory place:

```text
N shared read borrows
OR
1 exclusive mutable borrow
```

Never:

```text
mutable + mutable
mutable + shared
```

## 11.2 Shared Example

Valid:

```c
ref Foo *a = borrow(foo);
ref Foo *b = borrow(foo);
```

## 11.3 Mutable Example

Invalid:

```c
mut Foo *a = borrow_mut(foo);
mut Foo *b = borrow_mut(foo);
```

## 11.4 Move While Borrowed

Invalid:

```c
ref Foo *r = borrow(foo);
consume(move(foo));
use(r);
```

## 11.5 Mutation During Shared Borrow

Invalid when it can affect the borrowed place:

```c
ref Foo *r = borrow(foo);
mutate_owner(foo);
use(r);
```

## 11.6 Non-Lexical Lifetime Goal

Target:

```c
ref Foo *r = borrow(foo);
inspect(r);

/* r has no later use */

mut Foo *m = borrow_mut(foo);
update(m);
```

This should become valid once NLL analysis exists.

## 11.7 Implementation Order

```text
A. lexical borrow checking
B. CFG + liveness
C. last-use / NLL
D. limited interprocedural provenance
```

Do not attempt full Rust-level borrow analysis on day one.

---

# 12. Ownership Dataflow

Ownership must be a CFG dataflow analysis.

Example:

```c
own Foo *x;

if (cond)
    x = foo_create();

use(x);
```

After the branch:

```text
x = MAYBE_INITIALIZED
```

Use must be rejected if not proven initialized.

Conditional move:

```c
own Foo *x = foo_create();

if (cond)
    consume(move(x));

use(x);
```

After the branch:

```text
x = MAYBE_MOVED
```

Use must be rejected.

Use conservative joins:

```text
IN[BB]
transfer(BB)
OUT[BB]
join(predecessors)
```

---

# 13. Partial Moves

Do not implement arbitrary partial moves in the first ownership MVP.

Initially:

```c
struct S {
    own Foo *a;
    own Bar *b;
};

move(s.a);
```

may produce:

```text
error: partial move is not supported by this Crug version
```

Later:

- represent move paths;
- track field initialization;
- drop only initialized fields.

---

# 14. Drop and Deterministic Destruction

## 14.1 Goal

An owned resource must be destroyed exactly once unless ownership is transferred or explicitly released.

## 14.2 Destructor Sources

Support:

1. built-in known allocators/destructors;
2. Crug type metadata;
3. FFI metadata;
4. `adopt(expr, destructor)`;
5. attributes or future type declarations.

## 14.3 Adopt

```c
own Foo *foo = adopt(foo_create(), foo_destroy);
```

Meaning:

- raw/unmanaged return becomes owned;
- `foo_destroy` becomes its destruction operation.

## 14.4 Release

```c
Foo *raw = release(foo);
```

After release:

```text
foo = MOVED
```

No automatic destruction remains.

## 14.5 Implicit Drop

Source:

```c
void f(void)
{
    own Foo *x = foo_create();
    work(x);
}
```

Conceptual lowering:

```c
void f(void)
{
    Foo *x = foo_create();
    work(x);
    foo_destroy(x);
}
```

## 14.6 Drop Order

Default to reverse acquisition / reverse local declaration order unless the final language reference specifies otherwise.

## 14.7 Control Flow

Must test:

- `return`;
- `if/else`;
- `switch`;
- loops;
- `break`;
- `continue`;
- `goto`;
- nested scopes;
- early exit.

## 14.8 `setjmp` / `longjmp`

Because `longjmp` bypasses ordinary control flow:

- hybrid mode: warn if live owned resources may be skipped;
- strict mode: reject outside `unsafe` until dedicated semantics exist;
- kernel mode: preserve ordinary C semantics if ownership is not enabled.

---

# 15. Unsafe and Raw

## 15.1 Purpose

Create explicit boundaries where Crug cannot enforce its guarantees.

```c
unsafe {
    raw Foo *p = foreign_pointer();
    legacy_mutate(p);
}
```

## 15.2 Candidates Requiring Unsafe in Strict Mode

- dereferencing `raw`;
- arbitrary integer/pointer casts;
- dangerous ownership conversion;
- inline assembly affecting owned state;
- unknown lifetime extension;
- forced alias creation;
- selected FFI calls;
- `longjmp` across owned cleanup regions.

## 15.3 Runtime Cost

None.

`unsafe` is a compile-time marker.

---

# 16. C Compatibility and FFI

## 16.1 Core Principle

Normal C headers must remain usable:

```c
#include <stdio.h>
#include <sqlite3.h>
#include <openssl/ssl.h>
```

Existing libraries must not need recompilation.

## 16.2 Three Integration Levels

### Level 0 — Unmanaged C

```c
Foo *x = foo_create();
foo_destroy(x);
```

### Level 1 — Local Adoption

```c
own Foo *x = adopt(foo_create(), foo_destroy);
```

### Level 2 — Crug FFI Metadata

```c
own Foo *x = foo_create();
```

because metadata tells Crug that the return is owned and how to destroy it.

## 16.3 Sidecar Format

Define `.crugffi`.

Conceptual syntax:

```text
fn foo_create
    returns owned Foo*
    drop foo_destroy;

fn foo_destroy
    consumes arg0;

fn foo_read
    borrows arg0;

fn foo_update
    mut_borrows arg0;
```

The format must eventually support:

- opaque types;
- ownership;
- nullability;
- out parameters;
- callbacks;
- retained callbacks;
- shared/refcounted APIs;
- thread affinity;
- Send/Sync properties;
- blocking calls.

## 16.4 Discovery Order

Suggested:

1. explicit `-fcrug-ffi=...`;
2. adjacent metadata;
3. configured system metadata directory;
4. unmanaged fallback.

Never infer complex ownership only from function naming.

## 16.5 Built-in C Library Knowledge

A small tested catalog may include:

```text
malloc -> owned, drop free
calloc -> owned, drop free
free -> consumes
realloc -> special
fopen -> owned, drop fclose
fclose -> consumes
```

Keep the built-in list intentionally small.

---

# 17. Linux Kernel C Compatibility

This is a first-class language/compiler requirement.

## 17.1 Permanent Constraint

> **Compile existing Linux kernel C semantics without runtime overhead.**

This means Crug must be able to act as a drop-in compiler candidate for kernel-style C translation units without introducing managed-language behavior.

## 17.2 Required Semantic Compatibility

Crug plain-C/kernel mode must preserve GCC semantics for kernel-relevant constructs supported by the GCC baseline, including where applicable:

- GNU statement expressions:

```c
({ int x = expr; x + 1; })
```

- `typeof`, `__typeof__`, and supported variants;
- `__auto_type`;
- computed goto / labels as values;
- designated initializers;
- flexible array members;
- zero-length arrays where GNU C supports them;
- nested GCC attributes;
- `__attribute__((...))`;
- `__builtin_*` functions;
- `__builtin_constant_p`;
- `__builtin_expect`;
- overflow builtins;
- atomic builtins;
- explicit memory barriers expressed via builtins or asm;
- `volatile` semantics;
- inline `asm`;
- `asm goto`;
- register constraints;
- clobber lists;
- section placement;
- alignment/packing;
- weak symbols;
- aliases;
- visibility;
- function attributes;
- target-specific attributes;
- `noreturn`;
- `cold` / `hot`;
- `always_inline` / `noinline`;
- architecture-specific inline helpers;
- kernel-style macro metaprogramming.

The preferred implementation strategy is **not to reimplement these**. It is to preserve GCC C frontend behavior and avoid Crug changes that interfere with it.

## 17.3 No Runtime Overhead Definition

For a translation unit that uses no runtime Crug feature:

```bash
crugc -fcrug-kernel -ffreestanding foo.c
```

must not automatically add:

```text
libcrug
__crug_init
__crug_task_*
__crug_channel_*
TLS scheduler state
heap metadata
reference-count metadata
hidden locks
hidden worker threads
hidden bounds checks
```

The object file must contain no Crug runtime dependency unless the source explicitly uses a feature requiring one.

## 17.4 Freestanding Requirements

Must support toolchain usage compatible with:

```text
-ffreestanding
-nostdlib
-nostartfiles
-fno-builtin or selective builtin policies where requested
custom linker scripts
section-oriented linking
architecture-specific flags
```

Crug must not assume libc availability.

## 17.5 Kernel ABI and Layout

Kernel mode must preserve:

- C object layout;
- bitfield behavior as defined by the selected GCC target/dialect;
- packed layout;
- explicit alignment;
- section attributes;
- symbol names;
- calling conventions;
- inline asm constraints.

No Crug metadata may leak into kernel-visible structs.

## 17.6 Crug Ownership in Kernel Code

Long-term, the following should be possible:

```c
own struct object *obj = kmalloc(...);
```

only if:

- ownership semantics are explicitly enabled;
- destruction can be expressed using kernel-compatible operations such as `kfree` or custom kernel destructors;
- no `libcrug` is needed;
- no hidden allocation is introduced;
- no hidden locking is introduced.

Example conceptual metadata:

```text
kmalloc -> owned
kzalloc -> owned
kfree   -> consumes
```

This metadata must be profile-specific and architecture/config aware where necessary.

## 17.7 Kernel Borrowing

`ref` and `mut` may eventually be useful in kernel code because they are compile-time concepts.

However:

- they must not imply runtime synchronization;
- `mut` is not a spinlock;
- `ref` is not RCU;
- ownership rules must not claim safety across kernel concurrency primitives unless the compiler formally models the required synchronization.

Kernel concurrency semantics are not automatically solved by the borrow checker.

## 17.8 Kernel Goroutines

The ordinary user-space `libcrug` goroutine runtime is forbidden in kernel mode.

If a future kernel-specific task backend is created, it must be opt-in and separate:

```text
-fcrug-kernel-tasks
```

It must map explicitly to kernel-compatible scheduling primitives.

This is outside the initial kernel-compatibility goal.

## 17.9 Kbuild Integration Goal

Create a documented experimental integration path:

```text
make CC=crugc ...
```

or a wrapper mode preserving GCC-compatible argument behavior.

The initial milestone is not necessarily booting an entire kernel.

The staged goal is:

```text
1. compile isolated kernel-style tests
2. compile selected translation units
3. compile one subsystem
4. compile broad kernel object corpus
5. full kernel build
6. boot smoke test
7. architecture expansion
```

## 17.10 Kernel Differential Testing

Compare GCC and Crug plain-C/kernel builds for selected translation units.

Check:

- successful compilation;
- warnings/diagnostics differences;
- symbol tables;
- section names;
- section sizes;
- relocations;
- undefined symbols;
- no `__crug_*` symbols;
- assembly for sensitive primitives;
- object size;
- stack usage where measurable.

Byte-identical objects are not a universal requirement, but differences must be understood.

## 17.11 Kernel No-Overhead CI Gate

Create a CI check that fails if a kernel/plain-C object unexpectedly references:

```text
__crug_
libcrug
pthread
malloc   # when not present in original semantics/profile
```

The exact forbidden-symbol list must be profile-aware.

## 17.12 Kernel Test Corpus

Maintain:

```text
gcc/testsuite/crug.dg/kernel/
```

Include representative tests for:

```text
statement expressions
typeof
container_of-style macros
asm
asm goto
volatile MMIO
barriers
packed structs
section attributes
computed goto
builtins
designated initializers
flexible arrays
zero-length GNU arrays
per-CPU-like macros where practical
atomic primitives
branch prediction helpers
```

Also support an optional external Linux-kernel compile corpus in CI.

---

# 18. Shared Ownership

Shared ownership is explicit and later than unique ownership.

Possible syntax:

```c
shared Foo *x;
```

or library abstraction:

```c
arc<Foo> x;
```

Do not add hidden reference counting to every owned object.

Shared ownership operations may include:

```c
clone(x);
drop(x);
```

This runtime cost must be explicit.

---

# 19. Slices

## 19.1 Goal

Replace fragile pointer-length pairs:

```c
void process(char *ptr, size_t len);
```

with:

```c
void process(slice<char> data);
```

## 19.2 Representation

Default conceptual layout:

```c
struct {
    T *ptr;
    size_t len;
};
```

## 19.3 ABI

A Crug slice is not automatically ABI-equivalent to two separate C parameters.

FFI may use explicit lowering or metadata.

## 19.4 Bounds Checks

In checked modes:

```c
x = data[i];
```

requires:

```text
i < data.len
```

But:

- kernel/plain-C mode must not inject slice checks into ordinary C pointers;
- optimization should eliminate provably redundant checks;
- unchecked/raw slice access may require explicit unsafe syntax later.

---

# 20. Option

Target:

```c
option<T>
```

Variants:

```text
some(T)
none
```

Possible niche optimization for non-null pointer-like types:

```text
none    -> NULL
some(p) -> p
```

Only after ABI rules are formalized.

---

# 21. Result

Target:

```c
result<T, E>
```

Construction:

```c
return ok(value);
return err(error);
```

Later propagation:

```c
own Config *cfg = load_config()?;
```

The first implementation may require explicit `match`.

---

# 22. Algebraic Enums and Match

Target:

```c
enum Message {
    Text(own char *),
    Image(own Image *),
    Quit,
};
```

`match` must:

- understand active variants;
- be exhaustive unless wildcard exists;
- drop only the active owned fields.

Crug-native ADT layout must be documented separately from the C ABI.

---

# 23. Generics

Later target:

```c
T max<T>(T a, T b);
vector<int>;
vector<Foo>;
```

Prefer monomorphization for static generics unless an ADR chooses another strategy.

Ownership v1 must not depend on generics.

---

# 24. Traits / Interfaces

Non-MVP.

Keep significantly simpler than Rust.

Target use cases:

- generic constraints;
- static interfaces;
- optional dynamic dispatch;
- no class inheritance.

---

# 25. Crug Goroutines / Tasks

## 25.1 Philosophy

A Crug goroutine is not an OS thread.

```c
go worker();
```

creates a lightweight schedulable task.

## 25.2 M:N Scheduler

Target:

```text
many Crug tasks
      |
      v
Crug scheduler
      |
 +----+----+----+
 |         |    |
worker   worker worker
thread   thread thread
```

Tasks should greatly outnumber OS worker threads.

## 25.3 MVP Scheduler Rules

First implementation:

- portable core scheduler;
- Linux x86-64 PAL backend first;
- worker count defaults to CPU count;
- configurable worker count;
- cooperative yielding;
- task suspension on Crug channel operations;
- simple queues before work stealing.

The scheduler must call:

```c
crug_pal_thread_create(...);
crug_pal_thread_join(...);
crug_pal_wait_u32(...);
crug_pal_wake_one(...);
```

not:

```text
pthread_create
clone
CreateThread
```

directly.

## 25.4 Stack Model

MVP recommendation:

- stackful fibers/tasks;
- stack memory obtained via PAL;
- guard page where platform supports it;
- configurable fixed initial stack;
- no automatic growable stack initially.

Later research:

- segmented stacks;
- growable stacks;
- stack copying;
- sanitizer fiber integration.

## 25.5 Context Switch

CPU/ABI-specific implementation lives under:

```text
libcrug/arch/
```

Examples:

```text
x86_64/sysv
x86_64/windows
aarch64
riscv64
```

Tests must verify:

- callee-saved registers;
- stack alignment;
- floating/SIMD ABI state as required;
- sanitizer compatibility;
- unwind/debug interaction where supported.

## 25.6 Argument Capture

Source:

```c
go worker(a, b);
```

Conceptual lowering:

```c
struct __crug_task_frame_17 {
    typeof(a) a;
    typeof(b) b;
};
```

Rules differ for:

- copied scalar;
- copied aggregate;
- moved owned value;
- borrowed value;
- raw pointer.

---

# 26. Platform Abstraction Layer — PAL

Portability is a permanent architecture invariant.

## 26.1 Layout

```text
libcrug/
├── core/
│   ├── scheduler/
│   ├── task/
│   ├── scope/
│   ├── channel/
│   ├── sync/
│   └── timer/
│
├── pal/
│   ├── include/
│   │   └── crug_pal.h
│   ├── linux/
│   ├── windows/
│   ├── darwin/
│   ├── freebsd/
│   └── baremetal/
│
└── arch/
    ├── x86_64/
    │   ├── sysv/
    │   └── windows/
    ├── aarch64/
    └── riscv64/
```

## 26.2 Forbidden APIs in Core

`libcrug/core` may not directly reference:

```text
pthread_*
clone
clone3
futex
epoll
io_uring
kqueue
HANDLE
CreateThread
WaitOnAddress
VirtualAlloc
mach_*
```

## 26.3 Minimal PAL API

Threads:

```c
typedef struct crug_pal_thread crug_pal_thread;

int crug_pal_thread_create(
    crug_pal_thread **out,
    void (*entry)(void *),
    void *arg);

void crug_pal_thread_join(crug_pal_thread *thread);
uint32_t crug_pal_cpu_count(void);
```

Wait/wake:

```c
void crug_pal_wait_u32(
    _Atomic uint32_t *addr,
    uint32_t expected);

void crug_pal_wake_one(_Atomic uint32_t *addr);
void crug_pal_wake_all(_Atomic uint32_t *addr);
```

Memory:

```c
void *crug_pal_memory_reserve(size_t size);
bool crug_pal_memory_commit(void *ptr, size_t size);
bool crug_pal_memory_protect_guard(void *ptr, size_t size);
void crug_pal_memory_release(void *ptr, size_t size);
```

Time:

```c
uint64_t crug_pal_clock_monotonic_ns(void);
void crug_pal_sleep_ns(uint64_t ns);
```

Polling:

```c
struct crug_pal_poller;

struct crug_pal_poller *crug_pal_poller_create(void);
int crug_pal_poller_register(...);
int crug_pal_poller_unregister(...);
int crug_pal_poller_wait(...);
```

## 26.4 OS Backends

### Linux

Possible implementation:

```text
threads     pthread initially, clone3 later if justified
wait/wake   futex
memory      mmap / mprotect
poller      epoll
advanced    io_uring optional
```

### macOS / Darwin

```text
threads     pthread
memory      mmap / mprotect
poller      kqueue
wait/wake   appropriate Darwin primitive
```

### FreeBSD

```text
threads     pthread
memory      mmap
poller      kqueue
```

### Windows

```text
threads     Win32 thread API or _beginthreadex
wait/wake   WaitOnAddress / WakeByAddress*
memory      VirtualAlloc / VirtualProtect
poller      IOCP
timers      QPC / waitable timers
```

Do not emulate POSIX merely for architectural neatness.

### Bare Metal

May provide:

```text
no threads
custom allocator
hardware timer
platform-specific wait
```

Compile-time ownership remains usable.

## 26.5 OS vs CPU/ABI

These are separate axes:

```text
Linux x86-64   = PAL Linux + arch x86_64 SysV
Windows x86-64 = PAL Windows + arch x86_64 Windows
macOS x86-64   = PAL Darwin + arch x86_64 SysV
Linux AArch64  = PAL Linux + arch AArch64
macOS AArch64  = PAL Darwin + arch AArch64
```

## 26.6 Portability Matrix Goal

| Platform | x86-64 | ARM64 | Priority |
|---|---:|---:|---:|
| Linux | Yes | Yes | P0 |
| Windows | Yes | Later | P0/P1 |
| macOS | Legacy/where applicable | Yes | P1 |
| FreeBSD | Yes | Possible | P1/P2 |
| Android | target-dependent | Yes | P2 |
| iOS | — | Yes | P2 |
| Bare metal | BSP-dependent | BSP-dependent | P2 |
| Linux RISC-V | — | RISC-V | P2 |

## 26.7 Portable I/O

Future scheduler I/O must use:

```text
Crug scheduler
      |
      v
crug_pal_poller
      |
 +----+------+------+
 |           |      |
epoll      kqueue   IOCP
```

No OS poller leaks into scheduler semantics.

## 26.8 PAL Contract Tests

Run the same PAL contract suite on every supported backend:

```text
thread create/join
wait/wake
memory reserve/commit/protect/release
monotonic time
poller wakeup
shutdown
error translation
```

## 26.9 CI Lint

CI must fail if OS APIs appear in `libcrug/core`.

---

# 27. Ownership + Goroutines

## 27.1 Ownership Transfer

```c
own Job *job = job_create();

go worker(move(job));
```

After spawn:

```text
caller.job = MOVED
task.frame.job = OWNED
```

## 27.2 Missing Move

If worker expects ownership:

```c
go worker(job);
```

must fail:

```text
error: owned value passed to concurrent task without ownership transfer
help: use move(job)
```

## 27.3 Borrow into Unstructured Task

Usually invalid:

```c
ref Foo *r = borrow(foo);
go worker(r);
```

unless the borrow is proven to outlive the task.

## 27.4 Borrow Within Structured Scope

Potentially valid:

```c
scope {
    ref Foo *r = borrow(foo);
    go inspect(r);
}
```

because children cannot outlive the scope.

## 27.5 Mutable Borrow

May only be transferred when exclusive access is preserved.

---

# 28. Send / Sync Model

Use Rust-like concepts, possibly with Crug-specific names.

Meaning:

```text
Send:
    value may cross worker/thread boundary

Sync:
    shared references may be used concurrently
```

Do not assume raw pointers are automatically safe.

FFI metadata may state:

```text
type OpenGLContext
    !send
    !sync;
```

Kernel note:

- Crug `Send`/`Sync` does not automatically model RCU, spinlocks, preemption state, IRQ state, or lockdep semantics.
- Kernel-specific concurrency contracts require separate design.

---

# 29. Thread Affinity

Future syntax/API may allow pinned tasks.

Example:

```c
go pinned render_thread();
```

Do not include in the first scheduler milestone.

---

# 30. Channels

## 30.1 Types

```c
chan int a;
chan own Job *jobs;
```

## 30.2 Capacity

```c
chan int a = channel(0);
chan int b = channel(128);
```

## 30.3 Send

```c
send(ch, value);
send(jobs, move(job));
```

## 30.4 Receive

```c
int x = recv(ch);
own Job *job = recv(jobs);
```

Ownership transfers to the receiver.

## 30.5 Close

Specify precisely:

- who may close;
- send-after-close behavior;
- receive-after-close behavior;
- how completion is represented.

Do not copy Go behavior implicitly; specify it.

## 30.6 Implementation

Start with correctness:

- mutex/scheduler coordination;
- ring buffer for buffered channels;
- rendezvous for unbuffered channels;
- waiter queues.

Use lock-free designs only after profiling.

---

# 31. Select

Non-MVP.

Must address:

- multiple wait registration;
- winner selection;
- cancellation of losing registrations;
- fairness;
- default;
- no waiter use-after-free.

---

# 32. Structured Concurrency

`scope` is semantically important.

```c
scope {
    go fetch_a();
    go fetch_b();
}
```

Conceptual behavior:

```text
create task group
spawn children
...
scope exit
wait all children
destroy group
```

This allows the borrow checker to prove:

```text
child lifetime <= scope lifetime
```

---

# 33. Blocking C Calls

A user-space Crug task that performs a blocking C call may block its worker.

MVP:

- document this;
- cooperative scheduler.

Later:

- blocking pool;
- `blocking { ... }`;
- poller integration;
- FFI `blocking` metadata.

Kernel profile does not use the user-space runtime.

---

# 34. `libcrug` Runtime

## 34.1 Logical Components

```text
libcrug_base
libcrug_task
libcrug_channel
libcrug_sync
```

Physical organization remains:

```text
core/
pal/
arch/
```

Link only required components.

## 34.2 Portability

Core calls only:

```text
crug_pal_*
crug_arch_*
```

## 34.3 Internal ABI

Prefix internal runtime symbols:

```text
__crug_
```

Examples:

```c
__crug_task_spawn(...)
__crug_task_yield(...)
__crug_scope_enter(...)
__crug_scope_leave(...)
__crug_channel_create(...)
__crug_channel_send(...)
__crug_channel_recv(...)
```

Do not promise stability before 1.0.

## 34.4 Runtime Allocation

Every hidden runtime allocation must be documented and benchmarked.

Compile-time ownership itself needs no runtime.

## 34.5 Runtime Availability

Targets may support:

```text
ownership = yes
goroutines = no
```

The compiler should diagnose unavailable runtime features cleanly.

## 34.6 Kernel Rule

`-fcrug-kernel` must not link user-space `libcrug`.

---

# 35. Lowering

## 35.1 Main Principle

Eliminate Crug-specific semantic constructs before or during transition to ordinary GCC IR wherever possible.

```text
Crug source
   |
semantic checks
   |
lowering
   |
   +-- move -> assignment / SSA rename
   +-- borrow -> ordinary address/pointer representation
   +-- drop -> ordinary call
   +-- match -> branch/switch
   +-- Option/Result -> aggregate/tag representation
   +-- go -> runtime spawn call
   +-- channel -> runtime calls
   |
GENERIC / GIMPLE
```

## 35.2 Zero-Cost Compile-Time Features

`move(a)` must not become:

```c
__crug_move(a);
```

`borrow(a)` must not increment a refcount.

`mut` must not take a lock.

## 35.3 Kernel Lowering Rule

In kernel mode:

- ordinary C lowers exactly through normal GCC C paths;
- ownership-only constructs must lower to ordinary kernel-compatible operations;
- user-space runtime calls are forbidden.

---

# 36. Optimization Strategy

## 36.1 Performance Goal

The goal is not:

> Crug is always faster than C.

The goal is:

> Crug should match natural C performance and may allow stronger optimization when its static guarantees provide more information than ordinary unannotated C.

## 36.2 Alias Analysis

Example:

```c
void add(
    ref float *a,
    ref float *b,
    mut float *out,
    size_t n);
```

Potential facts:

- `out` has exclusive mutable access;
- conflicting aliases are not live.

Compare against C:

```c
void add(
    const float * restrict a,
    const float * restrict b,
    float * restrict out,
    size_t n);
```

Crug must never emit no-alias assumptions stronger than its semantics prove.

## 36.3 Owned Arguments

Distinct simultaneously-owned resources may provide uniqueness facts, but subobject/interior-pointer rules must be formalized before exploiting them aggressively.

## 36.4 Bounds-Check Elimination

Safe slices may lower to checks.

Existing GCC passes should eliminate checks proven redundant.

Only add a Crug-specific elimination pass if standard optimization is insufficient.

## 36.5 Branch Probability

Failure paths for checks may be marked cold only through correct GCC mechanisms.

## 36.6 Option Niche

Later optimization.

## 36.7 Result Copy Elimination

Use GCC SRA/copy propagation/RVO-like mechanisms where possible.

## 36.8 LTO

Correctness of ownership/borrow checking must not depend on LTO.

Optimization metadata across LTO must be separately designed and tested.

## 36.9 Kernel Optimization Constraint

Plain kernel-C code must not become slower merely because the compiler executable is `crugc`.

Benchmark and inspect sensitive kernel-style primitives:

- barriers;
- atomics;
- bit operations;
- inline accessors;
- branch prediction helpers;
- packed/unaligned helpers;
- spin-like primitives represented in test corpus.

---

# 37. Diagnostics

Example:

```text
error: use of moved value 'job'
  42 | process(job);
     |         ^~~
note: ownership was transferred here
  39 | go worker(move(job));
     |           ^~~~~~~~~
help: remove the later use or transfer another owned value
```

Borrow conflict:

```text
error: cannot create mutable borrow of 'buffer'
note: shared borrow remains live here
```

Requirements:

- exact source location;
- variable/place name;
- move/borrow origin;
- path explanation when needed;
- suggestions only when correct.

Kernel compatibility diagnostics must not flood valid GNU C code with Crug-only warnings when Crug features are disabled.

---

# 38. Developer Dumps

Add:

```bash
-fdump-crug
-fdump-crug-ownership
-fdump-crug-borrow
-fdump-crug-drop
-fdump-crug-lower
-fdump-crug-kernel-compat
```

Example:

```text
FUNCTION process

PLACE job
  BB2 IN: OWNED
  BB2 move@42 -> MOVED
  BB3 IN: MOVED
```

---

# 39. Test Strategy

Create:

```text
gcc/testsuite/crug.dg/
```

Suggested structure:

```text
crug.dg/
├── c-compat/
├── kernel/
├── parser/
├── ownership/
├── borrow/
├── drop/
├── ffi/
├── slices/
├── result/
├── adt/
├── goroutines/
├── channels/
├── optimization/
├── abi/
└── diagnostics/
```

---

# 40. Plain C Regression Tests

Primary invariant:

```bash
crugc test.c
```

must preserve C semantics.

Use:

- GCC C tests;
- preprocessor tests;
- GNU C extension tests;
- ABI tests;
- internal C corpus.

Differential test:

```text
gcc source.c  -> A
crugc source.c -> B

observable behavior(A) == observable behavior(B)
```

---

# 41. Linux Kernel Compatibility Tests

## 41.1 Required Invariant

For plain kernel-style C:

```text
runtime overhead = zero
mandatory Crug symbols = zero
semantic changes = zero
```

## 41.2 Kernel Syntax Tests

Test:

```text
GNU statement expression
typeof / __typeof__
__auto_type
asm
asm goto
volatile
computed goto
attributes
packed/aligned/section
builtins
designated initialization
zero-length arrays
flexible arrays
container_of-like patterns
atomic builtins
barriers
branch hints
```

## 41.3 Object-Level Tests

Inspect with standard binary tools:

```text
symbols
sections
relocations
undefined references
size
disassembly
```

Reject unexpected Crug runtime references.

## 41.4 Freestanding Link Tests

Use:

```text
-ffreestanding
-nostdlib
-nostartfiles
```

with minimal link scripts where practical.

## 41.5 External Kernel Corpus

Optional CI job:

- checkout a pinned Linux kernel source revision;
- compile selected translation units with GCC and Crug;
- compare success, diagnostics, symbols, sections, relocations;
- expand the corpus gradually.

## 41.6 Full Kernel Milestones

```text
K0: syntax microtests
K1: selected headers compile
K2: selected translation units
K3: subsystem object build
K4: broad kernel object corpus
K5: full kernel compile
K6: boot smoke test
K7: second architecture
```

---

# 42. Parser Tests

For every contextual word:

Positive with feature:

```c
own Foo *p;
```

Plain identifier:

```c
int own = 1;
```

Macro cases:

- macro expands to `own`;
- macro parameter named `own`;
- typedef interactions;
- function pointer declarators;
- arrays;
- `const`, `volatile`, `_Atomic`, `restrict`.

---

# 43. Ownership Tests

Minimum matrix:

```text
create -> use -> drop
create -> move -> destination use
use-after-move ERROR
double move ERROR
consume -> later use ERROR
conditional move
conditional initialization
loop move
early return
nested scopes
shadowing
parameter transfer
return transfer
```

---

# 44. Borrow Tests

Test:

```text
many shared borrows OK
two mutable borrows ERROR
shared + mutable ERROR
mutable + shared ERROR
move while borrowed ERROR
drop while borrowed ERROR
lexical end
NLL last-use
return local borrow ERROR
return parameter-derived borrow OK
```

---

# 45. Drop Tests

Use destructors that record calls.

Verify:

```text
exactly once
reverse order
branches
return
goto
break
continue
switch
loops
move suppresses source drop
destination drop occurs
partial initialization behavior
```

---

# 46. FFI Tests

Use an internal fake C library for reproducibility.

Test:

```text
owned return
consuming parameter
borrow
mutable borrow
out-owned parameter
opaque type
callback
refcount model later
```

Optional integration examples may use SQLite or other system libraries, but core tests must not depend on them being installed.

---

# 47. ABI Tests

Compare GCC C and Crug:

```text
sizeof
alignof
offsetof
symbol names
calling convention
return convention
function pointers
variadic calls
struct passing
```

Test both directions:

```text
C caller -> Crug callee
Crug caller -> C callee
```

---

# 48. Goroutine Tests

Test:

```text
spawn one
spawn many
argument passing
nested spawn
yield
scope wait
task completion
owned argument transfer
runtime shutdown
```

Stress:

```text
10
1,000
100,000 tasks
```

Do not set arbitrary scale targets before measuring the implementation.

---

# 49. Channel Tests

Test:

```text
capacity 0
capacity 1
capacity N
SPSC
MPSC
MPMC
close
blocked send
blocked receive
owned transfer
scheduler shutdown with waiters
```

---

# 50. Sanitizers

For runtime code, run:

```text
ASan
UBSan
TSan where fiber model integration is correct
```

Use sanitizer fiber hooks where necessary.

Sanitizer cleanliness is not formal proof.

---

# 51. Fuzzing

Fuzz:

1. contextual parser;
2. ownership state transitions;
3. `.crugffi` parser;
4. channel state machine;
5. scheduler queue logic;
6. kernel-compat parser regression cases.

Goals:

```text
no compiler crash
no ICE
no corrupt diagnostic state
no runtime memory corruption
```

---

# 52. Optimization Tests

Every optimization requires:

```text
semantic test
IR test
assembly/codegen inspection
benchmark
```

Alias benchmark:

```text
C unqualified
C restrict
Crug ref/mut
```

Kernel codegen benchmark:

```text
GCC kernel-style C
Crug kernel-style C
```

---

# 53. Benchmarks

## 53.1 Compiler

Measure:

```text
compile time
peak compiler memory
borrow-check analysis cost
kernel-compat compile cost
```

## 53.2 Executable / Object

Measure:

```text
runtime
binary size
object size
allocations
branches
cache behavior where available
```

## 53.3 Concurrency

Measure:

```text
spawn
yield
context switch
channel ping-pong
buffered throughput
task memory
worker scaling
```

Compare against:

```text
C direct call
C pthread
C thread pool
Crug tasks
```

## 53.4 Kernel Profile

Measure:

```text
translation-unit compile time
object size
stack usage where available
critical microbenchmark assembly
unexpected symbol dependencies
```

---

# 54. CI Matrix

Initial:

```text
Linux x86-64
  -O0
  -O2
  -O3

ASan runtime
UBSan runtime
plain C compatibility
kernel compatibility
```

Then:

```text
Linux AArch64
Windows x86-64
macOS ARM64
FreeBSD x86-64
```

Every platform runs PAL contract tests.

Kernel compile corpus may initially run only on Linux.

---

# 55. Developer Build

Conceptual:

```bash
git clone <crug-gcc-fork> crug-gcc
cd crug-gcc

./contrib/download_prerequisites

mkdir build-crug
cd build-crug

../configure \
  --enable-languages=c \
  --disable-multilib \
  --enable-checking=yes \
  --prefix="$HOME/.local/crug"

make -j"$(nproc)"
```

Provide scripts:

```text
scripts/build-dev.sh
scripts/test-crug.sh
scripts/test-c-compat.sh
scripts/test-kernel-compat.sh
scripts/test-runtime.sh
```

---

# 56. Implementation Roadmap

---

## PHASE 0 — Clean GCC Baseline

### Goal

Create a reproducible GCC 16.2 fork with no Crug semantics.

### Tasks

1. pin source baseline;
2. document build;
3. build C frontend;
4. run selected GCC C tests;
5. record results;
6. create CI;
7. create Crug docs tree.

### Exit Criteria

- compiler builds;
- baseline tests recorded;
- no intentional codegen change.

---

## PHASE 0K — Kernel Compatibility Baseline

### Goal

Establish the Linux-kernel C constraint before adding new syntax.

### Tasks

1. define `-fcrug-kernel`;
2. make it runtime-free;
3. collect kernel-style GNU C microtests;
4. create `scripts/test-kernel-compat.sh`;
5. compile tests with GCC and Crug;
6. compare symbols/sections/relocations;
7. assert zero `__crug_*` references;
8. test `-ffreestanding -nostdlib` scenarios;
9. add initial Kbuild-wrapper experiment.

### Exit Criteria

- kernel syntax microtests pass;
- no runtime link;
- no hidden startup;
- plain-C kernel tests preserve expected semantics.

No ownership work begins until this baseline is stable.

---

## PHASE 1 — Driver and Feature Flags

### Goal

Introduce Crug options without semantic changes.

### Tasks

Add:

```text
-fcrug-ownership
-fcrug-borrow
-fcrug-goroutines
-fcrug-channels
-fcrug-kernel
-fcrug-modern
```

Add dump infrastructure.

### Exit Criteria

Flags parse.

C and kernel regression tests remain unchanged.

---

## PHASE 2 — Contextual `own`

### Goal

Parse:

```c
own Foo *p;
```

only when enabled.

### Tasks

- minimal parser hook;
- side metadata;
- dump type/decl info;
- declarator tests;
- kernel macro regression.

### Exit Criteria

- `own` is contextual;
- ABI unchanged;
- kernel/plain-C code using `own` as identifier still works where unambiguous.

---

## PHASE 3 — `move` and Use-After-Move

### Goal

First real static ownership safety.

### Initial Scope

- local variables;
- owned pointers;
- simple CFG.

### Tasks

- parse `move(expr)`;
- state tracking;
- reject implicit owned copy;
- reject double move;
- reject use-after-move;
- branch joins;
- diagnostics.

### Exit Criteria

Correct static rejection with zero runtime code.

---

## PHASE 4 — Automatic Drop

### Goal

RAII-like cleanup without GC.

### Tasks

- destructor metadata;
- `adopt`;
- `release`;
- normal scope exit;
- early return;
- branches;
- loops;
- goto policy;
- exactly-once tests.

### Kernel Requirement

If tested in kernel mode with ownership explicitly enabled, drop lowering must be ordinary kernel-compatible calls and must not link `libcrug`.

---

## PHASE 5 — `ref` / `mut`

### Goal

Lexical borrow checker.

### Tasks

- parse qualifiers;
- `borrow`;
- `borrow_mut`;
- shared tracking;
- exclusive mutable tracking;
- move/drop while borrowed;
- parameters;
- simple returns.

---

## PHASE 6 — CFG / NLL

### Goal

Useful non-lexical borrowing.

### Tasks

- liveness;
- last use;
- branch join;
- loops;
- provenance diagnostics.

---

## PHASE 7 — Strict / Unsafe / Raw

### Goal

Explicit unchecked boundaries.

### Tasks

- `unsafe {}`;
- `raw`;
- conversions;
- FFI rules;
- `longjmp` policy;
- inline asm policy.

### Kernel Note

Plain kernel C remains unchanged when strict ownership semantics are not enabled.

---

## PHASE 8 — FFI Metadata

### Goal

Safe integration of existing C APIs.

### Tasks

- `.crugffi` parser;
- ownership return;
- consumed args;
- borrowed args;
- out-owned;
- nullability;
- callbacks;
- metadata versioning.

---

## PHASE 8K — Kernel Ownership Metadata

### Goal

Experimentally support compile-time ownership for kernel allocators without runtime linkage.

### Candidate Contracts

```text
kmalloc/kzalloc family -> owned
kfree -> consumes
```

Only add contracts that are semantically correct for the selected kernel usage and configuration.

### Exit Criteria

- no `libcrug`;
- no hidden allocation;
- no hidden locking;
- generated cleanup lowers to normal kernel function calls.

---

## PHASE 9 — Slices

### Goal

Safe pointer + length abstraction.

### Tasks

- syntax;
- layout;
- indexing;
- bounds checks;
- slicing;
- FFI conversion;
- elimination benchmarks.

Kernel/plain-C pointers remain unaffected.

---

## PHASE 10 — Option / Result

### Goal

Safer nullable/error APIs.

### Tasks

- representations;
- constructors;
- match integration;
- `?` later.

---

## PHASE 11 — ADT + Match

### Goal

Tagged unions with compile-time safety.

### Tasks

- parser;
- layout;
- active discriminant;
- exhaustive match;
- drop active variant.

---

## PHASE 12 — PAL Before Advanced Runtime

### Goal

Make portability real before scheduler complexity.

### Tasks

1. define `crug_pal.h`;
2. add fake/test PAL;
3. add Linux PAL;
4. PAL contract tests;
5. CI lint forbidding OS APIs in core.

### Exit Criteria

Portable scheduler dependencies can be expressed entirely through PAL.

---

## PHASE 13 — Minimal Goroutine Runtime

### Goal

Operational `go fn(args)`.

### Tasks

- task core;
- worker threads through PAL;
- queues;
- x86-64 SysV context;
- PAL stack allocation;
- spawn lowering;
- yield;
- completion;
- shutdown;
- stress tests.

### Exit Criteria

100k trivial tasks may be tested without 100k OS threads.

No OS API appears directly in `libcrug/core`.

---

## PHASE 13B — Multi-OS PAL Proof

### Recommended Order

1. Windows x86-64;
2. macOS ARM64;
3. FreeBSD x86-64;
4. Linux AArch64;
5. Android ARM64;
6. additional targets.

### Exit Criteria

Core scheduler remains unchanged across ports.

---

## PHASE 14 — Structured Concurrency

### Goal

`scope { go ... }`.

### Tasks

- task groups;
- child tracking;
- scope wait;
- owned capture;
- borrowed capture.

---

## PHASE 15 — Channels

### Goal

Typed communication and ownership transfer.

### Tasks

- type syntax;
- buffered;
- unbuffered;
- blocked send/recv;
- close;
- ownership transfer;
- MPMC stress.

---

## PHASE 16 — Select

### Goal

Multi-channel waiting.

---

## PHASE 17 — Send/Sync

### Goal

Static cross-thread safety.

---

## PHASE 18 — Ownership/Noalias Optimization

### Goal

Exploit proven Crug uniqueness.

### Tasks

- study GCC restrict/alias representation;
- encode safe facts;
- vectorization tests;
- IPA tests;
- LTO tests;
- benchmarks.

---

## PHASE 18K — Kernel Codegen Parity

### Goal

Prove Crug plain-C/kernel mode does not impose codegen/runtime penalties.

### Tasks

- select kernel-style microbenchmarks;
- compare GCC vs Crug assembly;
- compare object sizes;
- compare undefined symbols;
- compare stack usage where available;
- investigate every unexpected Crug-caused difference.

### Exit Criteria

No systematic runtime overhead attributable to Crug plain-C/kernel mode.

---

## PHASE 19 — Advanced Concurrency Runtime

### Tasks

- work stealing;
- portable poller;
- epoll backend;
- optional io_uring backend;
- kqueue backend;
- IOCP backend;
- timers;
- blocking pool;
- pinned tasks;
- tracing;
- growable stack research;
- AArch64 context;
- RISC-V research;
- sanitizer fiber support;
- preemption research.

---

## PHASE 20 — Generics / Traits

Only after the core model is stable.

---

## PHASE 21 — Release Engineering

### Tasks

- language reference;
- grammar;
- C ABI guide;
- kernel compatibility guide;
- FFI guide;
- migration guide;
- runtime guide;
- performance guide;
- platform guide;
- package/version policy.

---

# 57. Per-Phase Quality Gate

Every phase requires:

```text
[ ] parser tests
[ ] positive semantic tests
[ ] negative semantic tests
[ ] diagnostics tests
[ ] C regression
[ ] kernel regression if relevant
[ ] ABI tests if relevant
[ ] runtime tests if relevant
[ ] sanitizer run if runtime
[ ] optimization validation if optimization
[ ] documentation
[ ] progress.md update
```

---

# 58. Performance Rules

## 58.1 Mandatory Zero-Cost Compile-Time Concepts

The following must not intrinsically require runtime work:

```text
own
ref
mut
move
borrow checker
Send/Sync checking
unsafe marker
```

Automatic destruction is allowed only because equivalent cleanup would be required in correct manual C.

## 58.2 Explicit Runtime-Cost Features

May cost runtime:

```text
slice bounds checks
goroutine spawn
scheduler
channel
select
mutex
shared refcount
```

## 58.3 Forbidden Hidden Costs

Never implement:

```text
borrow -> heap allocation
borrow -> refcount
move -> allocation
own -> mandatory control block
mut -> mutex
plain C -> hidden Crug initialization
kernel C -> hidden libcrug linkage
```

---

# 59. Safety and Undefined Behavior

Crug cannot make arbitrary legacy C safe merely by compiling it.

Distinguish:

```text
Safe Crug
Checked Crug
Unsafe Crug
Legacy C
Kernel C semantics
```

A raw/unmanaged FFI boundary may violate Crug assumptions.

Do not claim whole-program memory safety while unchecked code can break invariants.

---

# 60. Interaction with `restrict`

`mut` and `restrict` are not synonyms.

```text
mut:
    Crug exclusive borrow with a defined lifetime

restrict:
    C language aliasing contract
```

The compiler may translate proven `mut` semantics into alias information useful to GCC where safe.

---

# 61. Interaction with `const`

`ref Foo *` means no mutation through that borrow plus lifetime relation.

`const Foo *` is ordinary C constness.

Therefore:

```text
const != ref
```

---

# 62. `_Atomic`

Initial policy:

- preserve ordinary C `_Atomic`;
- limit `_Atomic(own T*)` until semantics are defined;
- never infer shared ownership from atomics.

Kernel atomic primitives remain governed by kernel C/builtin/asm semantics unless Crug-specific modeling is explicitly enabled.

---

# 63. `volatile`

Preserve C semantics.

Do not treat `volatile` as goroutine synchronization.

Kernel volatile/MMIO semantics must not be rewritten by Crug ownership logic unless explicitly supported.

---

# 64. Globals and Statics

Owned globals require initialization/destruction policy.

MVP may restrict them.

Kernel global/static C semantics must remain unchanged in plain kernel mode.

---

# 65. Signal Handlers

User-space runtime APIs are not async-signal-safe by default.

Document any safe subset.

Kernel mode is separate and must not reuse user-space signal assumptions.

---

# 66. `fork`

After `fork()` in a multithreaded runtime, scheduler state requires explicit policy.

Use `pthread_atfork` only behind the Linux/POSIX PAL if chosen.

Do not expose this detail in core.

---

# 67. `dlopen` / `dlsym`

Dynamically obtained symbols have unmanaged ownership unless metadata/wrappers provide contracts.

---

# 68. Debugger Support

GDB should continue to see ordinary C-like variables.

Later tooling may expose:

- ownership state;
- task state;
- channel state;
- scheduler queues.

Kernel debugging must remain compatible with ordinary C debug info when Crug features are not used.

---

# 69. Future Tooling

Possible tools:

```text
c2crug
crugfmt
crug-lsp
crug-doc
crug-bindgen
crug-ffi-check
crug-kernel-check
```

`c2crug` is specified separately.

---

# 70. Required Documentation Tree

```text
docs/crug/
    README.md
    LANGUAGE.md
    GRAMMAR.md
    OWNERSHIP.md
    BORROWING.md
    UNSAFE.md
    C-FFI.md
    ABI.md
    KERNEL-COMPATIBILITY.md
    FREESTANDING.md
    CONCURRENCY.md
    CHANNELS.md
    RUNTIME.md
    PORTABILITY.md
    PAL.md
    OPTIMIZATION.md
    TESTING.md
    COMPILER-INTERNALS.md
    progress.md
```

---

# 71. Example: Plain C Must Remain Plain C

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int own = 42;
    int go = 7;

    int *p = malloc(sizeof(*p));
    *p = own + go;

    printf("%d\n", *p);
    free(p);

    return 0;
}
```

Must compile normally with:

```bash
crugc program.c
```

without interpreting `own` or `go` as Crug syntax in these contexts.

---

# 72. Example: Progressive Ownership Migration

Step 1:

```c
Foo *foo = foo_create();
foo_use(foo);
foo_destroy(foo);
```

Step 2:

```c
own Foo *foo = adopt(foo_create(), foo_destroy);
foo_use(foo);
```

Step 3 with metadata:

```c
own Foo *foo = foo_create();
foo_use(borrow(foo));
```

No recompilation of the C library is required.

---

# 73. Example: Safe Concurrency

```c
struct Job {
    int id;
    own char *name;
};

void process(own struct Job *job)
{
    use(job);
}

void worker(chan own struct Job *jobs)
{
    for (;;) {
        own struct Job *job = recv(jobs);

        if (!job)
            break;

        process(move(job));
    }
}

int main(void)
{
    chan own struct Job *jobs = channel(128);

    scope {
        for (int i = 0; i < 8; ++i)
            go worker(jobs);

        for (int i = 0; i < 1000; ++i) {
            own struct Job *job = job_create(i);
            send(jobs, move(job));
        }

        close(jobs);
    }

    return 0;
}
```

This is a target-direction example, not necessarily MVP syntax.

---

# 74. Example: Kernel Plain-C Compatibility

A kernel-style file may contain constructs conceptually like:

```c
#define min_t(type, x, y) ({     \
    type __x = (x);              \
    type __y = (y);              \
    __x < __y ? __x : __y;       \
})

static __attribute__((always_inline))
unsigned long read_reg(volatile unsigned long *p)
{
    unsigned long v = *p;
    asm volatile("" : "+r"(v) :: "memory");
    return v;
}
```

Crug kernel mode must preserve the GCC C semantics of such constructs and must not insert Crug runtime calls.

Compile:

```bash
crugc -fcrug-kernel -ffreestanding -c kernel_style.c
```

Expected:

```text
no libcrug
no __crug_* undefined symbols
no worker runtime
no hidden heap allocation
```

---

# 75. Example: Optional Ownership in Freestanding/Kernel Code

Future target:

```c
own struct object *obj =
    adopt(kmalloc(sizeof(*obj), GFP_KERNEL), kfree);

if (!obj)
    return -ENOMEM;

initialize(obj);
```

The compiler may lower cleanup to a normal `kfree(obj)` on relevant exits.

Requirements:

```text
no libcrug
no runtime metadata
no hidden thread state
```

This feature must remain opt-in.

---

# 76. Example: Optimization

Crug:

```c
void transform(
    mut float *dst,
    ref float *src,
    size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = src[i] * 2.0f;
}
```

Compare with:

```c
void transform(
    float * restrict dst,
    const float * restrict src,
    size_t n)
{
    for (size_t i = 0; i < n; ++i)
        dst[i] = src[i] * 2.0f;
}
```

The goal is not magical performance beyond properly annotated C.

The goal is to let Crug derive useful optimizer facts from language guarantees.

---

# 77. First Concrete AI Harness Tasks

## TASK 001 — Pin GCC Baseline

- checkout GCC 16.2;
- record exact commit/tag;
- build C-only compiler;
- run selected C tests;
- write baseline into `progress.md`.

## TASK 002 — Kernel Compatibility Test Skeleton

Create:

```text
gcc/testsuite/crug.dg/kernel/
scripts/test-kernel-compat.sh
docs/crug/KERNEL-COMPATIBILITY.md
```

Add GNU C microtests.

## TASK 003 — `-fcrug-kernel`

Add the flag.

It must initially change **no C semantics** and link **no runtime**.

## TASK 004 — No-Runtime Object Check

Compile kernel/plain-C microtests.

Fail if object contains unexpected:

```text
__crug_*
```

## TASK 005 — Crug Directory Skeleton

Create:

```text
gcc/crug/
gcc/testsuite/crug.dg/
docs/crug/
```

## TASK 006 — Ownership Flag

Add:

```text
-fcrug-ownership
```

No parser change yet.

## TASK 007 — Crug Dump

Add:

```text
-fdump-crug
```

Print enabled Crug features.

## TASK 008 — Contextual `own`

Recognize declaration qualifier only when ownership enabled.

## TASK 009 — Ownership Metadata

Associate `owned` state with declaration.

## TASK 010 — Move Parser

Recognize:

```c
move(p)
```

## TASK 011 — Local Linear-State Checker

Detect:

```text
OWNED -> MOVED -> invalid use
```

## TASK 012 — CFG Ownership

Add branch joins.

After TASK 012, stop and write an architecture checkpoint ADR before implementing automatic drop.

---

# 78. Architecture Checkpoint After Ownership MVP

Answer in an ADR:

1. Where is Crug ownership metadata stored?
2. On what representation does checking operate?
3. When does it run?
4. How are source locations preserved?
5. How will drop lowering work?
6. How can alias facts reach GCC optimization safely?
7. Does the design survive LTO?
8. Does the design preserve kernel/GNU C semantics when features are disabled?
9. Does `-fcrug-kernel` remain runtime-free?
10. What C regressions occurred?
11. What kernel-compat regressions occurred?
12. What is compile-time overhead?

Do not continue if the architecture fails compatibility invariants.

---

# 79. Required Architecture Decision Records

Create:

```text
docs/crug/adr/
```

At minimum:

```text
0001-gcc-16.2-baseline.md
0002-contextual-keywords.md
0003-ownership-metadata-storage.md
0004-ownership-analysis-layer.md
0005-drop-lowering.md
0006-c-abi-policy.md
0007-ffi-metadata-format.md
0008-goroutine-stack-model.md
0009-scheduler-model.md
0010-noalias-lowering.md
0011-pal-contract.md
0012-os-backend-policy.md
0013-arch-context-switch-policy.md
0014-portable-io-poller.md
0015-linux-kernel-c-compatibility.md
0016-freestanding-no-runtime-policy.md
0017-kernel-ownership-policy.md
```

---

# 80. Major Risks

## Risk A — Excessive Changes to GCC C Frontend

Mitigation:

- minimal hooks;
- separate Crug modules;
- continuous C regression.

## Risk B — Breaking GNU C / Linux Kernel Semantics

Mitigation:

- kernel baseline before feature work;
- pinned kernel-style test corpus;
- plain-C path must bypass Crug semantics;
- object-level no-runtime checks;
- selected external kernel compile corpus.

## Risk C — Borrow Checker Scope Explosion

Mitigation:

- unique ownership first;
- lexical borrow;
- CFG;
- NLL later.

## Risk D — Drop and Nonlocal Control Flow

Mitigation:

- explicit restrictions;
- diagnostics;
- no false safety claim.

## Risk E — Incorrect Noalias Optimization

A false alias assumption can miscompile code.

Mitigation:

- semantic proofs;
- differential tests;
- adversarial alias tests;
- inspect GCC alias dumps.

## Risk F — Runtime Complexity

Mitigation:

- cooperative scheduler;
- portable PAL;
- fixed stack first;
- optimize later.

## Risk G — FFI Ambiguity

Mitigation:

- unmanaged default;
- explicit metadata;
- `adopt` / `release`.

## Risk H — Late Portability

Mitigation:

- PAL before advanced runtime;
- fake PAL;
- Windows/macOS port before sophisticated scheduler features.

## Risk I — Kernel Runtime Leakage

Mitigation:

- `-fcrug-kernel` hard no-runtime rule;
- symbol scanning in CI;
- freestanding link tests;
- separate kernel ownership metadata;
- user-space goroutine runtime forbidden.

## Risk J — Licensing

GCC is GPLv3.

The Crug compiler fork must comply.

`libcrug` licensing must be selected explicitly rather than assuming all GCC runtime exceptions automatically apply.

---

# 81. Permanent Invariants

These must be protected by tests for the lifetime of the project.

```text
INVARIANT 1
Crug features off => C semantics

INVARIANT 2
own/ref/mut/raw do not change ordinary pointer ABI

INVARIANT 3
move has no runtime call

INVARIANT 4
borrow has no hidden refcount

INVARIANT 5
owned resource is dropped at most once

INVARIANT 6
live mutable borrow is exclusive

INVARIANT 7
existing C libraries can be linked without recompilation

INVARIANT 8
goroutine runtime is absent when unused

INVARIANT 9
sending an owned value transfers ownership

INVARIANT 10
optimizer assumptions never exceed proven Crug semantics

INVARIANT 11
libcrug/core contains no direct OS primitive

INVARIANT 12
OS services pass through PAL

INVARIANT 13
CPU/ABI context switching is isolated in libcrug/arch

INVARIANT 14
ownership remains usable without goroutine runtime

INVARIANT 15
existing Linux-kernel C semantics compile without mandatory Crug runtime overhead

INVARIANT 16
-fcrug-kernel plain-C objects contain no unexpected __crug_* dependencies

INVARIANT 17
plain kernel C does not gain hidden heap allocation, locks, TLS, scheduler state, or bounds checks

INVARIANT 18
GNU C compatibility is preserved unless a documented Crug feature explicitly changes semantics
```

---

# 82. Pull Request Definition of Done

A PR is mergeable only if it includes:

- clear problem statement;
- technical solution;
- targeted tests;
- relevant regression suites;
- no unexpected warnings;
- no ICE;
- documentation for syntax/behavior changes;
- ABI note if applicable;
- runtime sanitizer run if applicable;
- kernel-compat run if parser/codegen/common frontend is touched;
- benchmark if performance-sensitive;
- `progress.md` update.

---

# 83. Technical Reference Topics for the Harness

The harness should study official GCC material relevant to the pinned baseline for:

- C frontend architecture;
- language frontend integration;
- libcpp;
- GENERIC;
- GIMPLE;
- SSA;
- alias analysis;
- points-to analysis;
- vectorizer;
- IPA;
- LTO;
- option tables;
- GCC diagnostics;
- DejaGNU tests;
- target backends;
- inline asm;
- GNU C extensions;
- freestanding compilation;
- `libgomp`;
- `libgo`;
- split-stack implementation and target limitations.

For Linux-kernel compatibility work, also maintain a pinned test source/corpus and document exactly which kernel version/configuration is used for external CI experiments.

---

# 84. Strategy Summary

Build Crug in this order:

```text
1. GCC C baseline
        |
2. Linux-kernel / GNU C compatibility baseline
        |
3. feature flags
        |
4. own
        |
5. move
        |
6. drop
        |
7. ref / mut
        |
8. borrow CFG / NLL
        |
9. unsafe / raw
        |
10. C FFI metadata
        |
11. kernel ownership experiment without runtime
        |
12. slices / Result / ADT
        |
13. PAL
        |
14. goroutine runtime
        |
15. multi-OS PAL proof
        |
16. scope
        |
17. channels
        |
18. select
        |
19. Send / Sync
        |
20. ownership-driven optimization
        |
21. kernel codegen parity
        |
22. advanced runtime
        |
23. generics / traits
        |
24. release hardening
```

Do not build the borrow checker, ADTs, advanced scheduler, and multi-OS I/O stack simultaneously.

First prove:

```text
A. Crug does not break C.
B. Crug does not break Linux-kernel-style GNU C.
C. Plain kernel C has zero Crug runtime overhead.
D. Crug can track one owned pointer with zero runtime state.
E. Crug can lower ownership semantics to ordinary GCC IR.
F. Runtime concurrency is cleanly isolated behind PAL.
```

---

# 85. Final Goal

Crug should let users choose the level of modernization.

Plain C:

```bash
crugc legacy.c
```

Ownership:

```bash
crugc -fcrug-ownership new_module.c
```

Safe ownership:

```bash
crugc -fcrug-ownership -fcrug-borrow safe_module.c
```

Go-like concurrency:

```bash
crugc -fcrug-goroutines server.c
```

Modern Crug:

```bash
crugc -fcrug-modern service.c
```

Linux kernel / freestanding compatibility:

```bash
crugc -fcrug-kernel -ffreestanding kernel_unit.c
```

The final philosophy is:

> **C when you want it. Rust-like safety when you ask for it. Go-like concurrency when you need it. Portable by design. Kernel-compatible without mandatory runtime overhead.**

Crug must not force developers to abandon:

- C ABI;
- existing C libraries;
- freestanding programming;
- low-level control;
- Linux-kernel-style GNU C semantics;
- native-code performance.

The long-term ambition is not to hide C.

It is to make C codebases incrementally safer, more analyzable, more concurrent, and easier to migrate while preserving the environments where C is valuable precisely because there is no runtime underneath it.
