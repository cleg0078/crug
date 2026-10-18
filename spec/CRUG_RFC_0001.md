# CRUG-RFC-0001
## Crug Language and Toolchain Architecture, Core Semantics, Compatibility, and Conformance

**Crug Project Request for Comments:** 0001  
**Intended status:** Crug Project Standard  
**Current status:** Draft / Experimental  
**Category:** Standards Track (Crug Project)  
**Version:** 0.1-draft  
**Date:** 2026-10-17  
**Reference compiler baseline:** GCC 16.2  
**Editors:** Crug Project  
**Supersedes:** None  
**Updates:** None  

---

## Status of This Memo

This document defines the initial standards framework for the Crug programming language, compiler, runtime, C interoperability model, Linux-kernel compatibility profile, and project conformance requirements.

This document is **not an IETF RFC**. It adopts selected RFC editorial and requirements conventions to make Crug specifications precise, reviewable, testable, and suitable for long-term standardization.

The current status is **Draft / Experimental**. Implementations are encouraged for validation, but provisional behavior may change before Crug 1.0.

---

## Abstract

Crug is a systems programming language and GCC-based compiler project designed to extend C with optional Rust-like memory-safety semantics and optional Go-like lightweight concurrency while preserving C source compatibility, the C ABI, existing C libraries, freestanding programming, native-code performance, and Linux-kernel-style GNU C semantics.

Crug is explicitly incremental. Plain C remains plain C. Ownership, borrowing, safe core types, and task-based concurrency are activated through explicit compiler features and MUST NOT impose hidden managed-runtime behavior on programs that do not use them.

The permanent architectural requirement is:

> **Compile existing Linux kernel C semantics without mandatory Crug runtime overhead.**

This RFC standardizes project architecture, terminology, feature activation, conformance classes, compatibility rules, core semantic invariants, runtime boundaries, kernel profile, versioning, implementation obligations, testing requirements, security considerations, and change control.

---

# 1. Requirements Language

The capitalized terms **MUST**, **MUST NOT**, **REQUIRED**, **SHALL**, **SHALL NOT**, **SHOULD**, **SHOULD NOT**, **RECOMMENDED**, **NOT RECOMMENDED**, **MAY**, and **OPTIONAL** are interpreted according to BCP 14, RFC 2119 as updated by RFC 8174.

Only uppercase use has normative meaning.

---

# 2. Scope

This RFC standardizes:

- the Crug architectural model;
- compatibility requirements;
- compiler profiles and feature activation;
- ownership and borrowing invariants;
- ABI transparency;
- C FFI principles;
- Linux-kernel/freestanding behavior;
- runtime/PAL boundaries;
- concurrency invariants;
- conformance classes;
- feature/version discovery;
- implementation-defined behavior;
- security and operational requirements;
- conformance testing;
- RFC governance and change control.

This RFC intentionally does not finalize every future syntax or ABI. Detailed stable semantics are expected to be split into focused follow-up RFCs.

---

# 3. Design Principles

## 3.1 C Is the Foundation

When Crug extensions are disabled, a conforming implementation MUST preserve the selected C/GNU C dialect semantics covered by its conformance claim.

## 3.2 Features Are Opt-In

Ownership, borrowing, safe slices, ADTs, task concurrency, and channels MUST NOT silently activate merely because `crugc` is used.

## 3.3 Compile-Time Safety Stays Compile-Time Where Possible

The following MUST NOT intrinsically require hidden runtime state:

```text
own
ref
mut
move
borrow checking
Send-like checking
Sync-like checking
unsafe markers
```

## 3.4 No Mandatory Garbage Collector

Crug MUST NOT require a garbage collector.

## 3.5 No Hidden Ownership Tax

`own` MUST NOT imply:

- reference counting;
- heap control blocks;
- GC headers;
- hidden mutexes;
- runtime ownership tables.

## 3.6 Runtime Features Are Modular

The user-space task/channel runtime MUST only be linked when used or explicitly requested.

## 3.7 C ABI Transparency Is Preferred

Compile-time-only pointer qualifiers SHOULD preserve the underlying C pointer ABI.

## 3.8 Portability Is Architectural

OS-specific runtime code MUST be isolated behind the Crug Platform Abstraction Layer (PAL).

CPU/ABI-specific context switching MUST be isolated under `libcrug/arch`.

## 3.9 Kernel Compatibility Is First-Class

A kernel-compatible implementation MUST compile Linux-kernel-style GNU C without mandatory Crug runtime overhead.

---

# 4. Goals

Crug aims to provide:

- C and GNU C compatibility;
- C ABI interoperability;
- existing C library reuse without recompilation;
- optional unique ownership;
- optional borrowing;
- move semantics;
- deterministic destruction;
- explicit unsafe/raw boundaries;
- safer slices;
- `Option`/`Result`;
- algebraic data types and matching;
- optional lightweight tasks;
- channels and structured concurrency;
- static cross-thread safety properties;
- no mandatory GC;
- no mandatory runtime for compile-time-only features;
- Linux-kernel/freestanding compatibility;
- multi-OS portability;
- optimization opportunities from stronger alias/lifetime information.

---

# 5. Non-Goals for 1.0 Core

Crug 1.0 Core does not require:

- Rust source compatibility;
- Go source compatibility;
- Rust procedural macros;
- the full Rust trait system;
- C++ classes/exceptions;
- mandatory reflection;
- mandatory GC;
- JIT compilation;
- kernel goroutines;
- package management;
- a new preprocessor language.

---

# 6. Terminology

**C Mode** — Crug extensions disabled; selected C/GNU C semantics apply.

**Crug Core** — Compile-time language facilities not requiring the task runtime.

**Owned value** — A value whose resource responsibility is uniquely assigned to one logical owner.

**Place** — A storage location tracked by ownership/borrow analysis.

**Move** — Ownership transfer that invalidates ownership use through the source place.

**Shared borrow** — Temporary non-owning read capability, conceptually `ref`.

**Mutable borrow** — Temporary exclusive mutable capability, conceptually `mut`.

**Raw pointer** — Pointer outside normal Crug ownership guarantees.

**Drop** — Destruction action associated with the end of an owned resource lifetime.

**Unmanaged C** — Ordinary C resource semantics not covered by a Crug ownership contract.

**FFI contract** — Metadata describing ownership/lifetime/concurrency behavior of an external C API.

**Crug task** — Lightweight schedulable execution context created by the Crug runtime.

**Worker** — OS thread used by the scheduler to execute Crug tasks.

**PAL** — Crug Platform Abstraction Layer.

**Kernel profile** — Freestanding/GNU-C-compatible mode with no mandatory Crug user-space runtime.

**Zero runtime overhead** — No hidden runtime bookkeeping attributable solely to the language feature.

---

# 7. Conformance Classes

Conformance classes are composable. An implementation MUST NOT claim a class unless all required behavior is implemented and tested.

## 7.1 C0 — C Compatibility

A C0 implementation:

- MUST support its documented C dialect;
- MUST preserve the supported target C ABI;
- MUST NOT inject Crug runtime dependencies;
- MUST preserve contextual Crug words as ordinary identifiers where C grammar permits.

## 7.2 K0 — Kernel/Freestanding Compatibility

A K0 implementation:

- MUST satisfy C0;
- MUST support the project kernel GNU-C corpus;
- MUST support freestanding compilation;
- MUST NOT require `libcrug`;
- MUST NOT add hidden heap allocation, TLS, locks, scheduler state, or bounds checks to plain C.

## 7.3 O1 — Ownership

An O1 implementation:

- MUST support `own`;
- MUST support move;
- MUST reject use-after-move;
- MUST reject double move;
- MUST reject implicit owned copy unless explicitly allowed;
- MUST implement deterministic drop for supported control flow.

## 7.4 B1 — Borrowing

A B1 implementation:

- MUST satisfy O1;
- MUST support shared borrow;
- MUST support exclusive mutable borrow;
- MUST reject incompatible live borrows;
- MUST reject move/drop while an incompatible borrow is live.

## 7.5 F1 — C FFI

An F1 implementation:

- MUST allow unmanaged C interoperability;
- MUST support versioned `.crugffi` metadata;
- MUST preserve external C ABI;
- MUST reject incompatible or malformed safety metadata deterministically.

## 7.6 T1 — Task Runtime

A T1 implementation:

- MUST provide lightweight tasks multiplexed over OS workers;
- MUST use PAL for OS interaction;
- MUST NOT require one OS thread per task.

## 7.7 C1 — Structured Concurrency

A C1 implementation:

- MUST satisfy T1;
- MUST provide structured task scope;
- MUST provide channels;
- MUST implement release-defined synchronization ordering;
- MUST enforce supported Send/Sync-like rules.

## 7.8 P1 — Production

A P1 implementation MUST satisfy the Crug 1.0 production conformance matrix published by the release RFC.

---

# 8. Versioning

## 8.1 Language Version

Crug uses:

```text
MAJOR.MINOR.PATCH
```

Before 1.0, incompatible changes MAY occur in minor versions.

After 1.0:

- MAJOR: incompatible stable-language change;
- MINOR: compatible feature addition;
- PATCH: compatible correction.

## 8.2 Separate Versions

Implementations MUST distinguish:

```text
compiler version
language version
runtime ABI version
FFI metadata version
```

## 8.3 Feature Stability

Features are:

```text
experimental
provisional
stable
deprecated
removed
```

Stable incompatible changes require a major language version unless correcting a serious safety/correctness flaw under an expedited RFC.

---

# 9. Source Model

## 9.1 Extensions

Implementations MUST accept `.c`.

Crug-native source MAY use `.crug`.

`.crug` MUST NOT silently imply every optional feature unless a release profile explicitly specifies that behavior.

## 9.2 Preprocessor

Crug retains the GCC C preprocessor model.

Crug 1.0 MUST NOT introduce an incompatible second macro language.

## 9.3 Encoding

UTF-8 is RECOMMENDED for Crug-native source.

Selected C-mode source encoding rules remain inherited from the implementation profile.

---

# 10. Feature Discovery

A conforming compiler MUST define version macros equivalent to:

```c
#define __CRUG__ 1
#define __CRUG_COMPILER__ 1
#define __CRUG_VERSION_MAJOR__ ...
#define __CRUG_VERSION_MINOR__ ...
#define __CRUG_VERSION_PATCH__ ...
```

When active, implementations SHOULD define:

```c
__CRUG_OWNERSHIP__
__CRUG_BORROW__
__CRUG_STRICT__
__CRUG_GOROUTINES__
__CRUG_CHANNELS__
__CRUG_KERNEL__
```

Runtime/FFI version discovery SHOULD be available:

```c
__CRUG_RUNTIME_ABI__
__CRUG_FFI_VERSION__
```

---

# 11. Reserved Namespaces

Reserved to Crug:

```text
__CRUG_*      predefined macros
__crug_*      runtime/internal symbols
crug_pal_*    PAL contract
crug_arch_*   architecture contract
```

User programs MUST NOT export symbols in these namespaces unless a future RFC explicitly permits it.

---

# 12. Compiler Profiles

## 12.1 Plain C

```bash
crugc file.c
```

Crug features OFF and no Crug runtime.

## 12.2 Ownership

```bash
crugc -fcrug-ownership file.c
```

## 12.3 Borrowing

```bash
crugc -fcrug-ownership -fcrug-borrow file.c
```

## 12.4 Modern

```bash
crugc -fcrug-modern file.c
```

The exact enabled set MUST be versioned.

## 12.5 Kernel/Freestanding

```bash
crugc -fcrug-kernel -ffreestanding file.c
```

This MUST NOT imply the user-space task runtime.

---

# 13. Contextual Keywords

Candidate contextual words include:

```text
own ref mut raw move borrow borrow_mut unsafe
go chan scope select match some none ok err shared
```

Existing C such as:

```c
int own = 1;
int go = 2;
```

MUST remain valid where grammar makes them ordinary identifiers.

---

# 14. Ownership Qualifiers and ABI

Conceptual syntax:

```c
own Foo *owner;
ref Foo *reader;
mut Foo *writer;
raw Foo *unchecked;
```

For ordinary pointer-qualified forms:

```text
sizeof(own Foo*) == sizeof(Foo*)
sizeof(ref Foo*) == sizeof(Foo*)
sizeof(mut Foo*) == sizeof(Foo*)
sizeof(raw Foo*) == sizeof(Foo*)
```

on the same target.

These qualifiers MUST NOT add hidden struct fields or hidden reference counts.

---

# 15. Ownership State

The compiler MUST distinguish at least:

```text
UNINITIALIZED
OWNED
MOVED
DROPPED
MAYBE_INITIALIZED
MAYBE_MOVED
```

Use of a moved or dropped owner is invalid.

Use of a maybe-moved owner MUST be rejected unless the control-flow analysis proves it valid at that use.

---

# 16. Move Semantics

Conceptual syntax:

```c
move(x)
```

A move:

1. transfers ownership;
2. invalidates ownership use through the source;
3. MUST NOT call a hidden move runtime function;
4. MUST NOT allocate memory.

Example:

```c
own Foo *a = create();
own Foo *b = move(a);
```

Subsequent ownership use of `a` MUST be rejected until reinitialized.

---

# 17. Copy Semantics

Owned values are non-copyable by default.

This is invalid:

```c
own Foo *a = create();
own Foo *b = a;
```

Plain C pointers remain copyable in ordinary C semantics.

---

# 18. Borrow Semantics

## 18.1 Shared Borrow

```c
ref Foo *r = borrow(owner);
```

Multiple compatible shared borrows MAY coexist.

## 18.2 Mutable Borrow

```c
mut Foo *m = borrow_mut(owner);
```

A mutable borrow MUST be exclusive against conflicting checked access.

## 18.3 No Runtime Borrow Tracking

Borrow creation MUST NOT:

- allocate;
- increment a reference count;
- acquire a mutex.

## 18.4 Non-Lexical Lifetimes

A production B1 compiler SHOULD use last-use/control-flow analysis rather than purely lexical borrow termination.

The final formal algorithm belongs in CRUG-RFC-0002.

---

# 19. Drop Semantics

Owned resources may have a destructor.

Conceptually:

```c
own Foo *x = create();
work(x);
```

may lower to the same externally observable cleanup behavior as:

```c
Foo *x = create();
work(x);
destroy(x);
```

Requirements:

- destructor MUST execute at most once per owned responsibility;
- move MUST suppress source-owner drop;
- `release` MAY relinquish ownership;
- `adopt` MAY create ownership from unmanaged resource + destructor contract.

The compiler MUST NOT claim deterministic cleanup through abnormal control flow it does not model.

---

# 20. Raw and Unsafe

`raw` marks unchecked pointer semantics in strict contexts.

`unsafe { ... }` marks code for which the programmer accepts responsibility for additional invariants.

`unsafe` MUST have no inherent runtime cost.

Unsafe/raw code can invalidate whole-program safety guarantees.

---

# 21. C Qualifier Interaction

`const != ref`

`restrict != mut`

`volatile` retains C semantics.

`_Atomic` retains C semantics.

A Crug compiler MAY derive optimizer information from Crug qualifiers only when the stronger assumption is proven by Crug semantics.

---

# 22. C ABI

ABI-transparent Crug pointer qualifiers MUST have the ordinary pointer calling representation.

Example:

```c
void consume(own Foo *p);
```

uses the same machine argument representation as:

```c
void consume(Foo *p);
```

A structure:

```c
struct S {
    own Foo *p;
    int x;
};
```

MUST have the same layout as the equivalent C structure with `Foo *p`.

Crug-native types such as slices, Results, ADTs, and channels MAY have a Crug-specific ABI that MUST be documented before stability is claimed.

---

# 23. C Interoperability

Unannotated C APIs remain usable.

Unknown ownership is unmanaged.

Existing `.a` and `.so` libraries MUST NOT require recompilation merely to be called from Crug.

A trusted sidecar format:

```text
.crugffi
```

MAY describe:

- owned return;
- consumed argument;
- shared borrow;
- mutable borrow;
- out-owned parameter;
- destructor;
- nullability;
- refcount semantics;
- callbacks;
- thread affinity;
- blocking behavior;
- Send/Sync-like properties.

AI or naming heuristics MUST NOT be treated as authoritative safety contracts.

---

# 24. FFI Metadata Versioning

`.crugffi` MUST be versioned.

Conceptual:

```text
crugffi 1;
```

Unknown mandatory semantics MUST be rejected unless forward-compatibility behavior is explicitly standardized.

Breaking format changes require a new major metadata version.

---

# 25. Safe Core Types

## 25.1 Slice

A slice conceptually contains pointer + length.

Checked indexing MAY have runtime bounds checks.

Plain C pointer indexing MUST NOT gain hidden bounds checks.

## 25.2 Option

`option<T>` models present/absent values.

Niche optimization MAY be used when semantically valid.

## 25.3 Result

`result<T,E>` models explicit success/error.

Final ABI and propagation syntax require a focused RFC.

## 25.4 ADTs

Tagged Crug enums MAY carry data.

Only the active variant's owned resources may be dropped.

---

# 26. Error Handling

This RFC defines no language exception mechanism.

Crug supports ordinary C error handling and may additionally support `Result`.

Compiler-generated runtime checks MUST document their failure behavior.

---

# 27. C Memory Model

Ordinary C memory, volatile behavior, atomics, and unmanaged data races inherit the selected C/GCC model unless Crug explicitly strengthens it.

Crug ownership does not automatically make raw shared memory race-free.

`own`, `ref`, and `mut` MUST NOT inject implicit synchronization.

---

# 28. Tasks and Scheduler

`go` creates a lightweight Crug task, not semantically an OS thread.

A task runtime SHOULD use M:N scheduling:

```text
many Crug tasks
      |
scheduler
      |
fewer OS worker threads
```

Crug 1.0 MUST NOT promise undocumented scheduling fairness or task order.

Whether scheduling is cooperative, preemptive, or hybrid is implementation-defined unless visible semantics require otherwise.

---

# 29. Structured Concurrency

A structured scope guarantees:

```text
child lifetime <= scope lifetime
```

A scope MUST NOT complete normally until its required child tasks have completed.

This rule may enable safe borrowed captures into child tasks.

Cancellation and task-failure propagation require final definition in CRUG-RFC-0003.

---

# 30. Channels

Channels are typed communication/synchronization objects.

The concurrency standard MUST define:

- buffered/unbuffered behavior;
- blocking behavior;
- close semantics;
- send-after-close behavior;
- receive-after-close behavior;
- ownership transfer;
- cancellation;
- destruction;
- memory ordering.

Minimum invariant:

> A successful send of an owned value transfers that ownership responsibility exactly once.

---

# 31. Synchronization Ordering

The concurrency standard MUST define happens-before relationships.

At minimum, Crug 1.0 is expected to define ordering for:

```text
matching channel send -> receive
child completion -> successful scope exit
runtime synchronization primitives
```

The compiler MUST NOT infer race freedom beyond standardized rules.

---

# 32. Send / Sync-Like Properties

Crug requires concepts equivalent in purpose to:

```text
Send
Sync
```

Conceptually:

**Send** — value may cross worker/thread boundary.

**Sync** — shared reference may safely participate in concurrent access.

Raw pointers MUST NOT automatically gain these guarantees in strict mode.

---

# 33. Thread Affinity

FFI metadata MUST be capable of expressing thread-affine resources before C1 is Final.

The runtime/compiler MUST reject or correctly pin operations that would violate a declared affinity contract.

---

# 34. Runtime Architecture

Runtime organization:

```text
libcrug/
  core/
  pal/
  arch/
```

`core` contains scheduler/task/scope/channel logic.

`pal` contains operating-system services.

`arch` contains CPU/ABI-specific context switching.

---

# 35. PAL Requirements

`libcrug/core` MUST NOT directly depend on:

```text
pthread
clone/clone3
futex
epoll
io_uring
kqueue
CreateThread
WaitOnAddress
VirtualAlloc
Mach-specific primitives
```

The PAL SHOULD provide abstractions for:

```text
thread create/join
CPU count
wait/wake
memory reserve/commit/protect/release
monotonic clock
sleep
I/O polling
```

Expected platform families:

```text
Linux
Windows
Darwin/macOS
BSD
bare metal
```

---

# 36. Architecture Layer

OS and CPU are separate axes.

Examples:

```text
Linux x86-64   = Linux PAL + x86-64 SysV arch
Windows x86-64 = Windows PAL + x86-64 Windows arch
macOS ARM64    = Darwin PAL + AArch64 arch
```

A task context switch MUST obey the target ABI.

---

# 37. Runtime ABI

The Crug runtime ABI MUST have an independent version.

Internal symbols use:

```text
__crug_*
```

Crug 1.0 MUST publish:

- runtime ABI version;
- compatibility range;
- compiler/runtime mixing rules.

Known incompatible combinations MUST be rejected or clearly diagnosed.

---

# 38. Linux Kernel Profile

## 38.1 Permanent Requirement

> **Compile existing Linux kernel C semantics without mandatory Crug runtime overhead.**

## 38.2 Language Baseline

The project kernel validation baseline MUST track the documented GNU C dialect of the pinned supported Linux kernel source.

The Linux kernel documentation currently describes typical compilation using GCC with `-std=gnu11`, making GNU C11 the primary compatibility target for the current Crug kernel profile.

## 38.3 Required GNU C Areas

Validation MUST cover representative kernel use of:

```text
statement expressions
typeof / __typeof__
__auto_type
attributes
builtins
asm
asm goto
computed goto
volatile
atomics
barriers
packed/aligned structures
section placement
weak/alias symbols
flexible arrays
GNU zero-length arrays
designated initialization
macro metaprogramming
```

## 38.4 No Runtime Leakage

Plain kernel-profile source MUST NOT automatically introduce:

```text
libcrug
__crug_init
task scheduler
channels
worker threads
hidden heap allocation
hidden TLS
hidden mutexes
hidden bounds checks
```

## 38.5 Freestanding

Kernel profile MUST support compatible use of:

```text
-ffreestanding
-nostdlib
-nostartfiles
custom linker scripts
target flags
```

## 38.6 Optional Ownership

Ownership MAY be used in kernel source only when explicitly enabled.

It MUST lower to kernel-compatible operations and MUST NOT require user-space `libcrug`.

## 38.7 Kernel Concurrency

`mut` is not a spinlock, mutex, RCU guarantee, preemption barrier, IRQ rule, or lockdep annotation.

Kernel concurrency requires separate modeling.

## 38.8 Kernel Tasks

The user-space Crug task runtime MUST NOT be silently enabled in kernel profile.

Any kernel-task facility requires a separate RFC.

---

# 39. GCC Lowering Strategy

Crug SHOULD lower to ordinary GCC IR wherever possible:

```text
move    -> compile-time state + ordinary assignment/SSA effect
borrow  -> compile-time state + pointer representation
drop    -> ordinary call
match   -> branch/switch
go      -> explicit runtime spawn call
channel -> explicit runtime calls
```

New Crug-specific GIMPLE opcodes SHOULD NOT be introduced without a documented architectural need.

---

# 40. Optimization Contract

Optimizations MUST preserve Crug semantics.

The compiler MAY derive alias information from ownership/borrow guarantees.

A false noalias assumption is a correctness defect.

Bounds checks MAY be removed only when proven redundant.

Plain C code SHOULD NOT be systematically degraded solely because it is compiled by `crugc`.

Kernel-sensitive codegen SHOULD be compared with reference GCC output.

---

# 41. Implementation-Defined, Unspecified, and Undefined Behavior

## 41.1 Implementation-Defined

Implementations MUST document choices such as:

- target ABI details;
- default task stack size;
- default worker count;
- runtime resource-failure behavior;
- unstable Crug-native layouts.

## 41.2 Unspecified

Programs MUST NOT depend on unspecified choices such as:

- which runnable task runs first;
- worker assignment;
- internal allocation strategy.

## 41.3 Undefined Behavior

Crug inherits C undefined behavior for unmanaged operations unless Crug explicitly replaces it with:

- compile-time rejection;
- runtime check;
- defined trap/error;
- stronger semantics.

The language reference MUST distinguish these categories.

---

# 42. Diagnostics

A production compiler SHOULD identify:

- invalid operation;
- affected value/place;
- move/borrow origin;
- conflicting use;
- a fix suggestion when unambiguous.

Stable diagnostic IDs are RECOMMENDED.

A machine-readable diagnostic mode SHOULD include:

```text
diagnostic id
severity
message
file/line/column
related locations
suggested edit
feature
```

---

# 43. Build-System Compatibility

The compiler SHOULD remain compatible with common C build practices.

Targets include:

```text
CC=crugc
compile_commands.json
Autoconf probes
CMake
Meson
Kbuild
```

Feature macros SHOULD be preferred over compiler-name guessing.

---

# 44. Mixed-Mode Linking

One final binary MAY mix:

```text
plain C objects
Crug ownership objects
Crug task-runtime objects
foreign C objects
```

provided ABI/runtime compatibility holds.

Objects not using runtime features MUST NOT require `libcrug`.

---

# 45. LTO

Ownership/borrow correctness MUST NOT depend on LTO.

LTO MAY improve optimization.

Any Crug metadata crossing LTO boundaries MUST be versioned and validated.

---

# 46. Debug Information

ABI-transparent values SHOULD remain debuggable with ordinary C debugging tools.

Future tooling MAY expose:

```text
ownership state
borrow state
task state
channel state
```

without breaking ordinary debugger compatibility.

---

# 47. Native Thread Interoperability

Crug tasks MAY coexist with native C threads.

Before C1 becomes Final, the runtime standard MUST define:

- initialization;
- foreign-thread calls into runtime;
- task spawn from foreign thread;
- TLS assumptions;
- shutdown.

---

# 48. Signals, Fork, and Dynamic Loading

Runtime APIs are not automatically async-signal-safe.

Post-`fork()` behavior in a multithreaded task runtime MUST be documented.

`dlopen`/`dlsym` remain available; dynamically obtained APIs are unmanaged unless trusted metadata supplies contracts.

---

# 49. Security Considerations

Crug reduces some bug classes but does not guarantee whole-program safety.

Security-sensitive areas include:

- unsafe/raw code;
- incorrect FFI contracts;
- compiler unsoundness;
- drop bugs;
- incorrect alias optimizations;
- task stack/context code;
- scheduler queues;
- channel state;
- kernel code generation.

`.crugffi` metadata affecting safety is trusted input and MUST be treated accordingly.

Miscompilation, double drop, false noalias, or scheduler memory corruption are release-blocking defects.

---

# 50. Privacy Considerations

Crug defines no required telemetry.

The reference compiler/runtime MUST NOT require telemetry to compile or execute programs.

Optional telemetry in tooling SHOULD be explicitly documented and opt-in.

---

# 51. Operational Considerations

Production users SHOULD be able to discover:

```text
compiler version
language version
runtime ABI
enabled features
runtime requirements
```

The toolchain SHOULD provide an inspection mechanism for runtime linkage requirements.

---

# 52. Testing and Conformance

A P1 implementation MUST maintain suites for:

```text
C compatibility
GNU C compatibility
kernel compatibility
ownership
borrow
drop
unsafe/raw
FFI
safe core types
optimization
PAL
tasks
scope
channels
select
Send/Sync
multi-OS
kernel build
c2crug
fuzzing
sanitizers
performance
```

---

# 53. Differential Testing

Plain C SHOULD be compared between reference GCC and Crug.

Comparison MAY include:

```text
observable output
exit status
symbol tables
sections
relocations
ABI behavior
sensitive assembly
```

Bit-identical object files are not universally required.

---

# 54. Kernel Qualification Levels

```text
K0 syntax microtests
K1 selected headers
K2 selected translation units
K3 subsystem build
K4 broad object corpus
K5 full kernel build
K6 boot smoke
K7 second architecture
```

Each release MUST state its achieved level.

Crug 1.0 P1 is expected to reach at least K6 on the primary target unless the 1.0 release RFC explicitly changes this requirement.

---

# 55. Runtime Qualification Levels

```text
R0 PAL contract
R1 basic task
R2 high task count
R3 structured scope
R4 channels
R5 select
R6 multi-OS
R7 sanitizer/stress qualification
```

Release notes MUST state the runtime level.

---

# 56. Performance Qualification

Claims must be measured.

Required checks include:

```text
move -> no helper call
borrow -> no refcount
mut -> no lock
plain C -> no mandatory runtime
kernel C -> no mandatory runtime
```

Useful benchmark comparisons:

```text
ordinary C
C restrict
Crug
C pthread
C thread pool
Crug tasks
```

---

# 57. Fuzzing and Sanitizers

The project SHOULD continuously fuzz:

```text
parser
ownership analysis
borrow analysis
.crugffi parser
c2crug rewriter
scheduler
channel state machine
```

Runtime development SHOULD use ASan and UBSan.

TSan SHOULD be used where compatible with fiber/task context integration.

Historical crashes/miscompilations SHOULD become minimized regression tests.

---

# 58. Reference Implementation

The GCC-based `crugc` compiler is the initial reference implementation.

The standard MUST NOT intentionally depend on undocumented quirks of that implementation.

Independent implementations MAY claim conformance by satisfying the published specification and conformance suite.

---

# 59. Crug RFC Process

Normative stable-language changes MUST use the Crug RFC process.

RFC states:

```text
Draft
Review
Accepted
Final
Deprecated
Superseded
Rejected
Withdrawn
```

A Final RFC is part of the stable project standard for the releases that reference it.

---

# 60. Required Sections for Future RFCs

A normative Crug RFC SHOULD contain:

1. metadata/header;
2. status;
3. abstract;
4. motivation;
5. goals;
6. non-goals;
7. terminology;
8. requirements language;
9. detailed specification;
10. grammar/syntax where applicable;
11. static semantics;
12. dynamic semantics;
13. ABI impact;
14. runtime impact;
15. C compatibility impact;
16. kernel/freestanding impact;
17. portability impact;
18. security considerations;
19. performance considerations;
20. diagnostics;
21. alternatives;
22. backward compatibility;
23. migration strategy;
24. test plan;
25. implementation plan;
26. unresolved questions;
27. future possibilities;
28. registry/IANA considerations;
29. normative references;
30. informative references.

Sections not applicable SHOULD explicitly say so.

---

# 61. RFC Finalization Criteria

A normative RFC SHOULD NOT reach Final until:

```text
[ ] syntax is unambiguous
[ ] static semantics are specified
[ ] dynamic semantics are specified
[ ] compatibility impact is known
[ ] ABI/runtime effects are known
[ ] security risks are analyzed
[ ] conformance tests exist
[ ] reference implementation exists
[ ] implementation experience exists
[ ] unresolved questions are closed or explicitly deferred
```

---

# 62. Change Control

Stable semantics MUST NOT be changed solely by an implementation patch.

Required process:

1. problem statement;
2. RFC or RFC amendment;
3. compatibility analysis;
4. test update;
5. implementation;
6. release-note entry.

Emergency correctness/security changes MAY use an expedited process but MUST be documented.

---

# 63. Project Registries

The Crug project SHOULD maintain registries for:

```text
feature identifiers
compiler flags
predefined macros
attributes
.crugffi keywords
runtime ABI versions
diagnostic IDs
target profiles
feature stability
```

Registries prevent incompatible reuse.

---

# 64. Feature Registry Entry

Each stable feature SHOULD record:

```text
identifier
flag
feature macro
introduced version
stability
dependencies
runtime requirement
kernel compatibility
```

Example:

```text
Feature: ownership
Flag: -fcrug-ownership
Macro: __CRUG_OWNERSHIP__
Runtime: none
Kernel-compatible: yes, subject to profile rules
```

---

# 65. Compatibility Policy

Stable Crug 1.x releases SHOULD preserve stable source behavior.

ABI-transparent features MUST remain ABI-transparent.

Runtime ABI and FFI metadata have separate versioning.

No stable GCC plugin ABI is promised by this RFC.

---

# 66. Deprecation

Stable features SHOULD normally pass through:

```text
deprecation announcement
warning period
migration guidance
allowed removal boundary
```

before removal.

Security defects may require faster changes.

---

# 67. c2crug Relationship

`c2crug` is a migration tool, not the semantic authority.

Its transformations MUST conform to the Crug standard.

Safe mode MUST NOT convert uncertain ownership into a stronger guarantee.

AI suggestions MUST remain suggestions until validated.

---

# 68. AI-Assisted Development

AI MAY assist:

- implementation;
- tests;
- FFI metadata proposals;
- documentation;
- code review;
- migration.

AI output is not proof.

AI-generated safety contracts, optimizer assumptions, or concurrency rules MUST be validated mechanically or through project review.

---

# 69. Trusted Computing Base

Safety-relevant trusted components include:

```text
compiler
runtime
FFI metadata
target ABI description
PAL backend
arch context-switch backend
unsafe source
```

The project SHOULD minimize and audit this surface.

---

# 70. Supply Chain and Reproducibility

Production releases SHOULD provide:

```text
source tag
compiler version
runtime version
checksums
build instructions
license notices
SBOM
```

Reproducible builds are RECOMMENDED.

---

# 71. Licensing Considerations

The GCC-derived compiler must comply with applicable GCC licensing obligations.

`libcrug` licensing MUST be explicitly selected.

The project MUST NOT assume a GCC runtime exception automatically applies to newly created Crug runtime code.

FFI metadata SHOULD have a clear redistribution license.

This section is technical guidance, not legal advice.

---

# 72. IANA Considerations

No IANA action is required.

Crug registries are project-maintained registries, not IANA registries.

---

# 73. Alternatives Considered

## 73.1 New Compiler from Scratch

Not selected initially because it would require reimplementing a large amount of C/GNU C, ABI, optimization, backend, and tooling infrastructure.

## 73.2 LLVM-Only Frontend

Technically valid, but GCC is the reference base because GNU C/kernel compatibility is a primary project requirement.

## 73.3 Mandatory Rust-Like Safety

Rejected because incremental migration is a core goal.

## 73.4 Mandatory Go-Like Runtime

Rejected because kernel, freestanding, and no-runtime operation are core goals.

## 73.5 Garbage Collection

Rejected as a mandatory language/runtime dependency.

## 73.6 One OS Thread per `go`

Rejected because `go` is intended to create lightweight tasks.

---

# 74. Open Issues Before Crug 1.0 Final

The following MUST be resolved or explicitly deferred by Final RFCs:

1. formal place/move-path model;
2. NLL/lifetime inference rules;
3. partial moves;
4. partially initialized aggregate drop;
5. stable Crug-native ABI;
6. channel close/receive API;
7. channel memory ordering;
8. structured cancellation;
9. task failure/panic model;
10. public Send/Sync naming;
11. thread-affinity syntax;
12. runtime ABI compatibility rules;
13. formal `.crugffi` grammar;
14. slice-check failure behavior;
15. `Result` propagation syntax;
16. stable attribute syntax;
17. `.crug` default profile;
18. kernel allocator ownership contracts;
19. LTO metadata format;
20. debugger metadata conventions.

---

# 75. Required Follow-Up RFCs

Recommended standards series:

```text
CRUG-RFC-0002  Ownership, Move, Drop, and Borrow Semantics
CRUG-RFC-0003  Tasks, Structured Concurrency, Channels, and Memory Ordering
CRUG-RFC-0004  C FFI Metadata (.crugffi)
CRUG-RFC-0005  Linux Kernel and Freestanding Profile
CRUG-RFC-0006  Crug ABI and Runtime ABI
CRUG-RFC-0007  Slices, Option, Result, and ADTs
CRUG-RFC-0008  Send/Sync and Thread Affinity
CRUG-RFC-0009  Compiler Feature and Attribute Registry
CRUG-RFC-0010  Crug 1.0 Conformance and Release Profile
```

---

# 76. Standards Maturity Model

Crug uses:

**Experimental** — design validation; breaking changes expected.

**Proposed** — semantics mostly resolved, implemented, and tested.

**Stable** — meaningful implementation experience and compatibility review.

**Production Standard** — sustained operational experience, conformance evidence, compatibility evidence, and release qualification.

---

# 77. Acceptance Criteria for CRUG-RFC-0001

This RFC may move from Draft to Accepted when:

```text
[ ] architecture approved
[ ] conformance classes approved
[ ] versioning approved
[ ] reserved namespaces approved
[ ] kernel no-runtime invariant approved
[ ] PAL isolation approved
[ ] RFC governance approved
[ ] follow-up RFC split approved
```

It may move to Final when the core follow-up semantics and 1.0 conformance suite exist.

---

# 78. Normative References

**[RFC2119]** S. Bradner, *Key words for use in RFCs to Indicate Requirement Levels*, BCP 14, RFC 2119.

**[RFC8174]** B. Leiba, *Ambiguity of Uppercase vs Lowercase in RFC 2119 Key Words*, BCP 14, RFC 8174.

**[GCC]** GNU Compiler Collection documentation corresponding to the supported compiler baseline and target.

**[C]** Applicable ISO C standard selected by the Crug profile plus explicitly documented GNU C extensions.

---

# 79. Informative References

**[RFC7322]** H. Flanagan and S. Ginoza, *RFC Style Guide*, RFC 7322.

**[RFC2026]** S. Bradner, *The Internet Standards Process — Revision 3*, RFC 2026.

**[LINUX-LANG]** Linux Kernel Documentation, *Programming Language*, describing the kernel's GNU C usage and `-std=gnu11` practice.

**[CRUG-SPEC]** `CRUG_MASTER_SPEC_EN.md`.

**[CRUG-ROADMAP]** `CRUG_PRODUCTION_ROADMAP.md`.

**[C2CRUG]** `C2CRUG_MASTER_IMPLEMENTATION_PLAN.md`.

---

# Appendix A. Normative Examples

## A.1 Plain C Identifiers

```c
int main(void)
{
    int own = 1;
    int go = 2;
    return own + go;
}
```

C0 MUST treat these as normal identifiers in ordinary C contexts.

## A.2 Move

```c
own Foo *a = create();
own Foo *b = move(a);
```

Ownership use of `a` after the move MUST be rejected until reinitialized.

## A.3 Compatible Shared Borrow

```c
own Foo *a = create();
ref Foo *r1 = borrow(a);
ref Foo *r2 = borrow(a);
```

## A.4 Conflicting Borrow

```c
ref Foo *r = borrow(a);
mut Foo *m = borrow_mut(a);
```

B1 MUST reject overlapping incompatible borrows.

## A.5 Kernel No-Runtime Requirement

```bash
crugc -fcrug-kernel -ffreestanding -c unit.c
```

For plain C source, the object MUST NOT acquire an implicit Crug runtime dependency.

---

# Appendix B. Recommended Feature Macros

```text
__CRUG__
__CRUG_COMPILER__
__CRUG_VERSION_MAJOR__
__CRUG_VERSION_MINOR__
__CRUG_VERSION_PATCH__
__CRUG_OWNERSHIP__
__CRUG_BORROW__
__CRUG_STRICT__
__CRUG_GOROUTINES__
__CRUG_CHANNELS__
__CRUG_KERNEL__
__CRUG_RUNTIME_ABI__
__CRUG_FFI_VERSION__
```

Inactive feature macros SHOULD normally be undefined unless the feature registry specifies otherwise.

---

# Appendix C. Recommended Compiler Flags

```text
-fcrug-ownership
-fcrug-borrow
-fcrug-strict
-fcrug-slices
-fcrug-result
-fcrug-adt
-fcrug-goroutines
-fcrug-channels
-fcrug-kernel
-fcrug-modern
-fcrug-ffi=<path>
```

Developer diagnostics:

```text
-fdump-crug
-fdump-crug-ownership
-fdump-crug-borrow
-fdump-crug-drop
-fdump-crug-lower
-fdump-crug-kernel-compat
```

---

# Appendix D. Standards Checklist for New Features

Before a feature is stable:

```text
[ ] RFC exists
[ ] grammar documented
[ ] static semantics documented
[ ] dynamic semantics documented
[ ] ABI impact documented
[ ] runtime impact documented
[ ] C compatibility reviewed
[ ] kernel/freestanding impact reviewed
[ ] portability reviewed
[ ] security reviewed
[ ] performance measured
[ ] diagnostics specified
[ ] tests exist
[ ] implementation exists
[ ] unresolved issues documented
[ ] migration path documented
```

---

# Appendix E. Crug 1.0 Production Standard Gate

Before a P1 1.0 release:

```text
C compatibility          qualified
GNU C compatibility      qualified
kernel profile           qualified
ownership                qualified
borrowing                qualified
FFI                      qualified
PAL                      qualified
task runtime             qualified
structured concurrency   qualified
channels                 qualified
Send/Sync                qualified
Linux                    qualified
Windows                  qualified
macOS                    qualified
kernel full build        qualified
kernel boot smoke        qualified
fuzzing                  operational
sanitizers               operational
performance              published
security review          completed
documentation            completed
compatibility policy     published
```

---

# Appendix F. Editorial Guidance for Future Crug RFCs

Future authors SHOULD:

- write testable requirements;
- separate normative text from rationale;
- define failure behavior;
- identify undefined, unspecified, and implementation-defined behavior;
- state ABI effects;
- state runtime effects;
- state C compatibility effects;
- state kernel/freestanding effects;
- state portability effects;
- include positive and negative examples;
- include security considerations;
- include alternatives;
- include migration impact;
- split normative and informative references;
- list unresolved questions explicitly.

---

# Appendix G. Architecture Summary

```text
                         CRUG
                           |
             +-------------+-------------+
             |             |             |
             C          Safety       Concurrency
             |             |             |
        compatibility   ownership       tasks
        GNU C           borrowing       scope
        ABI             move/drop       channels
        kernel          safe types      select
             |             |             |
             +-------------+-------------+
                           |
                       GCC backend
                           |
              +------------+------------+
              |                         |
         no-runtime path             libcrug
              |                         |
     C / kernel / embedded       core + PAL + arch
```

The defining promise is:

> **Crug extends C without taking C away.**
