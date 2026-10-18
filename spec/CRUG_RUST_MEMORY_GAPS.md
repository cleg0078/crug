# CRUG — Rust-Grade Memory Safety Gap Specification

**Document type:** Design gap specification / RFC input  
**Status:** Draft  
**Purpose:** Define the missing memory-safety semantics required for Crug to provide Rust-grade ownership safety for the subset of features Crug chooses to support  
**Related documents:** `CRUG_RFC_0001.md`, `CRUG_MASTER_SPEC_EN.md`, `CRUG_PRODUCTION_ROADMAP.md`  
**Primary goal:** Close the semantic gaps between the current Crug ownership model and a production-grade, sound ownership/borrowing system

---

# 1. Scope

Crug already specifies the following core concepts:

```text
own
move
automatic drop
ref
mut
borrow
borrow_mut
raw
unsafe
N shared borrows OR one mutable borrow
move/drop forbidden while borrowed
planned NLL
Send/Sync-like concepts
```

This document defines the missing semantics required before Crug can credibly claim:

> **Rust-grade ownership memory safety for safe Crug code.**

The goal is **not** to clone Rust 1:1.

The goal is to provide equivalent safety guarantees for the subset of memory-management features that Crug supports.

Crug MAY deliberately reject some patterns that Rust accepts if doing so keeps the language simpler and remains memory-safe.

---

# 2. Design Goal

Crug should eventually guarantee, for safe Crug code:

```text
no use-after-free
no double free
no use-after-move
no dangling safe references
no mutable aliasing violations
no drop of partially moved resources
no invalid safe references
no unsound lifetime extension
no unsound ownership duplication
no optimizer assumptions stronger than language guarantees
```

These guarantees do not extend automatically across:

```text
raw pointers
unsafe blocks
incorrect FFI metadata
external C code
inline assembly
unmodeled kernel synchronization
```

---

# 3. Required Missing Semantic Areas

The following areas are REQUIRED before Rust-grade ownership safety can be claimed.

```text
1. Place and projection model
2. Move-path model
3. Partial moves
4. Partial initialization
5. Drop flags and field-sensitive drop
6. Reborrowing
7. Disjoint-place borrowing
8. Non-lexical lifetime semantics
9. Lifetime provenance and returned-borrow relationships
10. Lifetime subtyping
11. Variance
12. Drop checking
13. Temporary lifetime rules
14. Reference validity model
15. Pointer provenance model
16. Interior mutability
17. Copy / Clone / move-only semantics
18. Unsafe aliasing obligations
19. Pattern matching and ownership
20. Aggregate destruction order
21. Array/index borrowing rules
22. Union ownership rules
23. Self-referential structures policy
24. FFI lifetime contracts
25. Concurrency interaction with borrowing
```

---

# 4. Place Model

## 4.1 Motivation

A borrow checker cannot reason soundly about memory if it only tracks whole variables.

The compiler must understand distinct memory locations such as:

```c
s.a
s.b
array[i]
*p
p->field
obj.inner.value
```

These are called **places**.

## 4.2 Required Place Forms

The semantic model MUST be able to represent at least:

```text
local variable
function parameter
global/static
dereference
struct field
union field
array element
slice element
subslice
nested field projection
```

Conceptual representation:

```text
Place(root, projections...)
```

Example:

```c
state.player.inventory.items[i].name
```

may be represented as:

```text
root: state
projections:
  .player
  .inventory
  .items
  [i]
  .name
```

## 4.3 Static Disjointness

The compiler MUST be able to prove obvious disjointness.

Example:

```c
mut int *a = borrow_mut(s.a);
mut int *b = borrow_mut(s.b);
```

This SHOULD be valid when `a` and `b` refer to distinct fields.

## 4.4 Unknown Aliasing

If disjointness cannot be proven, safe Crug MUST reject conflicting mutable borrows.

## 4.5 Acceptance Criteria

```text
[ ] compiler distinguishes s.a from s.b
[ ] compiler distinguishes nested fields
[ ] compiler treats *p conservatively
[ ] compiler handles array/slice indexing conservatively
[ ] diagnostics print human-readable place paths
```

---

# 5. Move-Path Model

## 5.1 Motivation

Ownership must be tracked below whole-variable granularity.

Example:

```c
struct S {
    own Foo *a;
    own Bar *b;
};

struct S s = create_s();

own Foo *x = move(s.a);
```

After the move:

```text
s.a = MOVED
s.b = OWNED
s   = PARTIALLY_MOVED
```

## 5.2 Required Move-Path Tree

Conceptually:

```text
s
├── s.a
└── s.b
```

State:

```text
s.a -> MOVED
s.b -> OWNED
```

## 5.3 Parent-State Derivation

A parent aggregate is:

```text
FULLY_OWNED
PARTIALLY_MOVED
FULLY_MOVED
PARTIALLY_INITIALIZED
```

depending on child states.

## 5.4 Whole-Object Use

A partially moved aggregate MUST NOT be used as a whole value unless the operation is explicitly valid for partial state.

Example:

```c
consume(move(s));   // invalid if s.a was already moved
```

## 5.5 Field Use

Remaining owned fields MAY remain usable.

## 5.6 Acceptance Criteria

```text
[ ] field move does not invalidate unrelated fields
[ ] whole aggregate use rejected after partial move
[ ] nested partial moves supported
[ ] move-path joins work across branches
[ ] diagnostics identify the moved field path
```

---

# 6. Partial Initialization

## 6.1 Motivation

C commonly initializes aggregates incrementally.

Example:

```c
struct S s;

s.a = foo_create();
s.b = bar_create();
```

If initialization of `b` fails, `a` may still require destruction.

## 6.2 Required State

Per-field initialization state MUST be tracked when owned fields exist.

Conceptual state:

```text
s.a = INITIALIZED
s.b = UNINITIALIZED
```

## 6.3 Cleanup

Only initialized owned fields MUST be dropped.

## 6.4 Branching

Example:

```c
struct S s;

s.a = foo_create();

if (cond)
    s.b = bar_create();

return;
```

Compiler state:

```text
s.a = INITIALIZED
s.b = MAYBE_INITIALIZED
```

Drop logic must test or statically split paths as necessary.

## 6.5 Acceptance Criteria

```text
[ ] no drop of uninitialized field
[ ] initialized field always dropped
[ ] conditional initialization handled soundly
[ ] branch joins preserve initialization state
```

---

# 7. Drop Flags

## 7.1 Motivation

Partial moves and partial initialization require field-sensitive destruction.

## 7.2 Requirement

The compiler MUST implement a conceptual drop-flag model.

This does not necessarily require an actual runtime flag if control flow can prove the state statically.

## 7.3 Optimization Requirement

Drop flags SHOULD be optimized away when statically unnecessary.

## 7.4 Example

```c
struct S {
    own Foo *a;
    own Bar *b;
};

struct S s = create_s();
own Foo *x = move(s.a);
```

Scope exit must behave as:

```text
drop s.b
do not drop s.a
```

## 7.5 Acceptance Criteria

```text
[ ] moved fields are not dropped
[ ] initialized fields are dropped exactly once
[ ] static drop flags disappear where possible
[ ] generated code is inspected for avoidable runtime overhead
```

---

# 8. Reborrowing

## 8.1 Motivation

Mutable references often need to be temporarily lent again.

Conceptual example:

```c
mut Foo *a = borrow_mut(foo);
mut Foo *b = reborrow_mut(a);

use_mut(b);

/* a usable again after b ends */
```

## 8.2 Required Semantics

A reborrow MUST NOT create a second independent mutable capability.

Instead:

```text
a active
   |
reborrow
   v
a suspended
b active
   |
b ends
   v
a active again
```

## 8.3 Shared Reborrow from Mutable Borrow

This SHOULD be supported:

```c
mut Foo *m = borrow_mut(foo);
ref Foo *r = reborrow(m);
```

During `r`:

```text
m may not mutate conflicting state
```

After `r` ends:

```text
m may resume exclusive mutation
```

## 8.4 Acceptance Criteria

```text
[ ] mutable reborrow suspends parent mutable access
[ ] parent resumes after child borrow ends
[ ] shared reborrow from mut works
[ ] nested reborrows work
[ ] no duplicate mutable capability created
```

---

# 9. Disjoint-Place Borrowing

## 9.1 Struct Fields

This SHOULD be valid:

```c
mut int *x = borrow_mut(s.x);
mut int *y = borrow_mut(s.y);
```

when fields are distinct.

## 9.2 Arrays

This is harder:

```c
mut int *a = borrow_mut(arr[i]);
mut int *b = borrow_mut(arr[j]);
```

The compiler MAY reject unless it can prove:

```text
i != j
```

## 9.3 Slices

A future split API SHOULD allow explicit proof:

```c
(left, right) = split_mut(slice, mid);
```

producing disjoint mutable slices.

## 9.4 Acceptance Criteria

```text
[ ] disjoint struct fields supported
[ ] unknown array indices conservative
[ ] explicit slice splitting possible
[ ] unsafe escape exists for advanced cases
```

---

# 10. Non-Lexical Lifetimes

## 10.1 Requirement

Borrow lifetime SHOULD end at last semantic use, not merely end of lexical scope.

Example:

```c
ref Foo *r = borrow(foo);
inspect(r);

/* r dead here */

mut Foo *m = borrow_mut(foo);
update(m);
```

This SHOULD compile.

## 10.2 CFG-Based Analysis

Lifetime analysis MUST account for:

```text
branches
loops
early return
break
continue
goto where supported
```

## 10.3 Conservative Rule

If lifetime end cannot be proven, borrow remains live.

## 10.4 Acceptance Criteria

```text
[ ] last-use borrowing works
[ ] branch-sensitive lifetime works
[ ] loop cases are sound
[ ] false lifetime extension minimized
```

---

# 11. Lifetime Provenance

## 11.1 Motivation

Returned references must be tied to a valid source lifetime.

Example:

```c
ref Item *find(ref Container *c);
```

The result depends on `c`.

The compiler must know that.

## 11.2 Internal Lifetime Variables

Crug MAY keep user syntax implicit while modeling internal lifetime variables.

Conceptual:

```text
find<'a>(ref<'a> Container*) -> ref<'a> Item*
```

## 11.3 Multiple Inputs

Example:

```c
ref Foo *choose(ref Foo *a, ref Foo *b, bool cond);
```

The returned lifetime may depend on both.

The compiler MUST reject a returned reference whose provenance cannot be proven valid.

## 11.4 Acceptance Criteria

```text
[ ] returned borrow tied to source lifetime
[ ] local reference cannot escape local owner
[ ] multiple-input provenance represented
[ ] FFI may declare lifetime relation
```

---

# 12. Lifetime Subtyping

## 12.1 Requirement

A longer-lived borrow MAY be shortened safely.

Conceptually:

```text
'a: longer
'b: shorter

ref<'a> T -> ref<'b> T
```

when:

```text
'a outlives 'b
```

## 12.2 No Lifetime Extension

A borrow MUST NOT be converted to a longer lifetime without proof.

## 12.3 Acceptance Criteria

```text
[ ] safe shortening accepted
[ ] extension rejected
[ ] coercions documented
```

---

# 13. Variance

## 13.1 Motivation

Generics and nested references require formal rules.

Potential types:

```text
ref T
mut T
own T
slice<T>
option<T>
result<T,E>
```

## 13.2 Minimum Requirement

Before generics become stable, Crug MUST specify variance for:

```text
ref
mut
own
slice
```

## 13.3 Recommended Initial Model

A conservative model is acceptable.

Potential direction:

```text
ref T      covariant where safe
mut T      invariant in T
own T      depends on ownership/drop semantics
```

Final rules require a dedicated RFC.

## 13.4 Acceptance Criteria

```text
[ ] variance rules documented
[ ] no unsound lifetime coercion
[ ] generic tests cover variance
```

---

# 14. Drop Checking

## 14.1 Motivation

A destructor may access borrowed state.

Example conceptual type:

```c
struct Holder {
    ref Foo *foo;
};
```

If `Holder` owns a destructor that dereferences `foo`, `foo` must still be valid when the destructor runs.

## 14.2 Requirement

The compiler MUST ensure that any references required by destruction remain valid until destruction completes.

## 14.3 Conservative Alternative

Until full drop checking exists, Crug MAY restrict destructors on types containing borrowed fields.

## 14.4 Acceptance Criteria

```text
[ ] destructor cannot observe dangling ref
[ ] borrowed-field types have explicit drop rules
[ ] unsafe destructor escape is explicit
```

---

# 15. Temporary Lifetimes

## 15.1 Motivation

Temporary owners can create dangling borrows.

Example:

```c
ref Foo *r = borrow(create_foo());
```

Question:

```text
When is temporary owner destroyed?
```

## 15.2 Recommended Crug Rule

Crug SHOULD keep temporary lifetime rules simpler than Rust.

Recommended:

> A temporary owned value MUST NOT be borrowed beyond the full expression unless explicitly bound to a named owner.

Therefore:

```c
ref Foo *r = borrow(create_foo());
```

SHOULD be rejected.

User writes:

```c
own Foo *tmp = create_foo();
ref Foo *r = borrow(tmp);
```

## 15.3 Acceptance Criteria

```text
[ ] temporary borrow rules are simple
[ ] no implicit dangerous lifetime extension
[ ] diagnostics recommend binding temporary
```

---

# 16. Reference Validity Model

## 16.1 Safe `ref`

A safe `ref T *` SHOULD guarantee:

```text
non-null
properly aligned
points to valid T
valid for the borrow lifetime
aliasing contract satisfied
```

## 16.2 Safe `mut`

A safe `mut T *` SHOULD additionally guarantee:

```text
exclusive checked mutable access
```

for the duration of the borrow.

## 16.3 Raw Pointers

`raw T *` MAY be:

```text
null
dangling
misaligned
provenance-lost
aliased
```

Dereference is therefore unsafe.

## 16.4 Nullability

If nullable borrowed references are needed, use:

```text
option<ref T *>
```

rather than making every `ref` nullable.

## 16.5 Acceptance Criteria

```text
[ ] ref null construction rejected
[ ] invalid alignment not representable safely
[ ] raw->ref requires validation/unsafe contract
[ ] optimizer assumptions documented
```

---

# 17. Pointer Provenance

## 17.1 Motivation

Integer-pointer conversions can destroy ownership and alias knowledge.

Example:

```c
uintptr_t n = (uintptr_t)p;
Foo *q = (Foo *)n;
```

## 17.2 Requirement

Crug MUST define what provenance survives:

```text
pointer -> integer -> pointer
raw pointer casts
subobject pointers
allocation boundaries
FFI round trips
```

## 17.3 Recommended Initial Rule

Safe Crug SHOULD NOT recover ownership or safe borrowing merely from a pointer reconstructed from an integer.

Such a pointer becomes:

```text
raw
```

until validated/re-associated in unsafe code.

## 17.4 Optimization Rule

Noalias optimization MUST NOT assume provenance that the language does not guarantee.

## 17.5 Acceptance Criteria

```text
[ ] integer roundtrip semantics documented
[ ] ownership not magically recovered
[ ] raw provenance boundaries explicit
[ ] optimizer tests include provenance adversarial cases
```

---

# 18. Interior Mutability

## 18.1 Motivation

Some memory may legally change through a shared reference.

Examples:

```text
atomics
mutex-protected fields
reference counts
caches
MMIO
lazy initialization
```

## 18.2 Requirement

Crug needs a compiler-recognized interior-mutability primitive.

Possible conceptual form:

```text
interior_mut<T>
```

or a standard library/compiler intrinsic equivalent.

## 18.3 Rule

Shared `ref` must not imply immutable bytes for fields explicitly marked interior-mutable.

## 18.4 Optimization

Alias and const-propagation logic MUST respect interior mutability.

## 18.5 Acceptance Criteria

```text
[ ] safe shared access can coexist with explicit interior mutability
[ ] optimizer does not assume immutable memory incorrectly
[ ] atomics integrate correctly
[ ] interior mutability is explicit, not implicit
```

---

# 19. Copy / Clone / Move-Only Semantics

## 19.1 Motivation

Not every value should be move-only.

Example:

```c
int a = 10;
int b = a;
use(a);
```

remains valid.

## 19.2 Type Categories

Crug MUST distinguish:

```text
Copy
Clone-capable
Move-only
```

## 19.3 Recommended Rules

Potential baseline:

```text
integer scalar         Copy
floating scalar        Copy
plain raw pointer      Copy
ref pointer            Copy
mut borrow             NOT Copy
own pointer            NOT Copy
aggregate              Copy only if all fields Copy
```

## 19.4 Clone

Explicit clone may exist for types supporting duplication.

Example conceptual:

```c
shared Foo *b = clone(a);
```

## 19.5 Acceptance Criteria

```text
[ ] Copy types remain usable after assignment
[ ] own types move rather than copy
[ ] mut borrow cannot be duplicated
[ ] aggregate Copy derivation sound
```

---

# 20. Unsafe Aliasing Contract

## 20.1 Fundamental Rule

Unsafe code MAY create raw aliases.

However, unsafe code MUST preserve the validity of all live safe `ref` / `mut` guarantees that safe code may still rely on.

## 20.2 Example

If safe code holds:

```c
mut Foo *m;
```

unsafe code MUST NOT create and use a conflicting alias in a way that invalidates the exclusivity guarantee.

## 20.3 Responsibility

Violating this contract produces undefined behavior or another explicitly standardized unsafe failure mode.

## 20.4 Acceptance Criteria

```text
[ ] safe/unsafe boundary documented
[ ] raw aliases allowed
[ ] live safe references remain protected by unsafe obligations
[ ] optimizer contract matches language contract
```

---

# 21. Pattern Matching and Ownership

## 21.1 Motivation

Patterns may borrow, copy, or move fields.

## 21.2 Requirement

Each pattern binding MUST specify or infer one of:

```text
copy
borrow
mutable borrow
move
```

## 21.3 Default Rule

Crug SHOULD choose a simple predictable default.

Recommended:

```text
Copy field -> copy
owned move-only field -> move
explicit ref binding -> borrow
explicit mut binding -> mutable borrow
```

## 21.4 Partial Move Interaction

Pattern matching may create partial moves.

Move-path semantics MUST apply.

## 21.5 Acceptance Criteria

```text
[ ] moved pattern fields not later dropped twice
[ ] borrowed pattern fields remain valid
[ ] exhaustiveness and ownership interact soundly
```

---

# 22. Aggregate Destruction Order

Crug MUST standardize destruction order for:

```text
local variables
struct fields
array elements
ADT payloads
temporaries
function parameters
task-captured values
```

Recommended direction:

```text
locals: reverse declaration/acquisition order
struct fields: reverse declaration order
arrays: reverse index order
active ADT payload: defined per variant
```

The exact order MUST be frozen before 1.0.

Acceptance:

```text
[ ] order documented
[ ] tests assert order
[ ] panic/error/early-return paths documented
```

---

# 23. Array and Index Borrowing

## 23.1 Static Index

Distinct constant indices MAY be treated as disjoint.

Example:

```c
mut int *a = borrow_mut(arr[0]);
mut int *b = borrow_mut(arr[1]);
```

## 23.2 Dynamic Index

For:

```c
arr[i]
arr[j]
```

compiler SHOULD reject simultaneous mutable borrows unless disjointness is proven.

## 23.3 Safe Split API

Provide an explicit standard operation equivalent to:

```text
split_at_mut
```

for slices/arrays.

## 23.4 Acceptance Criteria

```text
[ ] constant distinct indices may work
[ ] dynamic unknown indices conservative
[ ] split API establishes disjointness
```

---

# 24. Union Ownership Rules

C unions complicate ownership because only one field is active.

## 24.1 Plain C Union

In compatibility mode, preserve ordinary C semantics.

## 24.2 Safe Crug Ownership

Safe ownership of multiple resource-bearing union members MUST NOT rely on unknown active-member state.

Options:

```text
reject owned union members
require explicit active tag
require unsafe
use Crug ADT instead
```

## 24.3 Recommended 1.0 Policy

Plain C unions remain allowed.

Safe Crug SHOULD reject owning union members unless wrapped in an explicitly tracked tagged representation.

## 24.4 Acceptance Criteria

```text
[ ] no double-drop through union alias
[ ] active owned field known before drop
[ ] unsafe escape available
```

---

# 25. Self-Referential Structures

## 25.1 Problem

Example:

```text
object contains pointer/reference into itself
```

Moving the object may invalidate internal addresses.

## 25.2 Recommended Initial Policy

Crug SHOULD NOT support safe movable self-referential owned aggregates automatically.

They require:

```text
stable allocation
pinning
handle indirection
unsafe code
```

## 25.3 Pinning

A `Pin`-like feature SHOULD NOT be added unless real Crug use cases require it.

## 25.4 Acceptance Criteria

```text
[ ] compiler does not falsely certify self-reference safety
[ ] unsafe/stable-allocation workaround documented
```

---

# 26. FFI Lifetime Contracts

`.crugffi` MUST eventually describe lifetime relationships.

Examples:

```text
return borrows arg0
arg1 stored until callback_unregister
callback retained
buffer borrowed only during call
```

Conceptual:

```text
fn lookup
    borrows arg0
    returns borrow_of arg0;
```

Acceptance:

```text
[ ] returned borrow source expressible
[ ] retained pointer contract expressible
[ ] callback retention expressible
[ ] unknown retention conservative
```

---

# 27. Callback Lifetime Semantics

Callbacks are a major C lifetime hazard.

Example:

```c
register_callback(ctx, callback, user_data);
```

Crug MUST distinguish:

```text
callback used only during call
callback retained after call
user_data retained
user_data copied
user_data consumed
```

A borrow MAY only be passed as retained user data if its lifetime is proven long enough.

Otherwise:

```text
owned/shared/static storage required
```

---

# 28. Concurrency and Borrowing

## 28.1 Cross-Task Borrows

A borrow sent to an unstructured task is invalid unless its lifetime is proven to outlive the task.

## 28.2 Structured Scope

This may be valid:

```c
scope {
    ref Foo *r = borrow(foo);
    go inspect(r);
}
```

because child lifetime is bounded.

## 28.3 Mutable Borrow Across Tasks

A mutable borrow may cross a task boundary only if exclusivity remains guaranteed.

## 28.4 Send/Sync

Borrowing rules must integrate with Send/Sync-like properties.

## 28.5 Acceptance Criteria

```text
[ ] unstructured escaping borrow rejected
[ ] scoped borrow accepted when valid
[ ] non-Send type rejected across workers
[ ] mutable borrow remains exclusive
```

---

# 29. Two-Phase Borrows

Rust supports special two-phase mutable borrow behavior for some method-call patterns.

Crug does NOT need this for initial Rust-grade safety.

Recommended policy:

```text
Crug 1.0 MAY reject code that would require two-phase borrowing.
```

If later introduced, it requires its own RFC.

This is an intentional simplification.

---

# 30. Temporary Borrow Simplification

Crug SHOULD prefer explicit named ownership over complex temporary extension rules.

Preferred:

```c
own Foo *tmp = create();
ref Foo *r = borrow(tmp);
```

Rejected or discouraged:

```c
ref Foo *r = borrow(create());
```

This is a deliberate simplification relative to Rust.

---

# 31. No Requirement to Copy Rust `Pin`

Rust's `Pin` primarily solves problems arising from self-referential values and async state machines.

Crug uses stackful tasks rather than Rust-style async state machines.

Therefore:

```text
Pin is NOT a required Crug 1.0 memory-safety feature.
```

Only introduce a pinning abstraction if concrete Crug-native use cases require it.

---

# 32. No Requirement to Copy Rust DSTs

Crug does not need full Rust dynamically sized types to achieve memory safety.

Slices may cover the primary use case.

Therefore:

```text
[T]
str
dyn Trait
```

do not need direct equivalents for Crug 1.0 safety completeness.

---

# 33. Memory Safety Claim Boundary

Crug may only claim Rust-grade memory safety under these conditions:

```text
safe Crug code only
trusted compiler
trusted runtime where used
trusted FFI metadata
no violated unsafe contracts
supported target/compiler configuration
```

The claim MUST NOT imply arbitrary legacy C is made safe automatically.

---

# 34. Compiler Architecture Requirements

The compiler should implement memory safety using:

```text
place analysis
move-path analysis
definite initialization
borrow state
liveness
lifetime provenance
drop elaboration
alias contract generation
```

Conceptual pipeline:

```text
GCC C / Crug AST
       |
       v
Place construction
       |
       v
Move paths
       |
       v
Ownership dataflow
       |
       v
Borrow/lifetime analysis
       |
       v
Drop checking
       |
       v
Drop elaboration
       |
       v
Crug lowering
       |
       v
standard GCC GENERIC/GIMPLE
```

---

# 35. Required Internal Analyses

At production maturity, Crug should have explicit internal modules equivalent to:

```text
crug-place.cc
crug-move-path.cc
crug-init.cc
crug-ownership.cc
crug-borrow.cc
crug-lifetime.cc
crug-provenance.cc
crug-dropck.cc
crug-drop.cc
crug-variance.cc
crug-copy.cc
```

Exact filenames are implementation-specific.

---

# 36. Testing Matrix

Every missing feature must receive:

```text
positive test
negative test
branch test
loop test where relevant
diagnostic test
optimizer test where relevant
FFI interaction test where relevant
kernel-mode interaction test where relevant
```

---

# 37. Mandatory Negative Tests

At minimum:

```text
use-after-partial-move
double-drop after field move
move while reborrow active
mutable alias through disjointness failure
return local borrow
lifetime extension
raw-to-ref without validation
null ref creation
misaligned ref creation
unsafe alias violates live mut
drop object with dangling borrowed field
retained callback with short borrow
cross-task borrow escape
```

---

# 38. Mandatory Positive Tests

At minimum:

```text
move one field, use another
partial initialization cleanup
nested reborrow
shared reborrow from mutable borrow
disjoint struct field mutable borrows
NLL last-use
returned borrow tied to input
safe lifetime shortening
Copy scalar assignment
scope-bounded cross-task ref
```

---

# 39. Optimization Safety Tests

The optimizer must be tested against:

```text
raw aliases
integer pointer roundtrip
interior mutability
shared refs
mutable refs
partial moves
reborrows
FFI calls
volatile
atomics
```

A compiler optimization that relies on stronger assumptions than the semantic model provides is a P0 bug.

---

# 40. Acceptance Milestones

## M-MEM-1 — Place Model

```text
[ ] projections
[ ] disjoint fields
[ ] diagnostics
```

## M-MEM-2 — Move Paths

```text
[ ] partial moves
[ ] partial initialization
[ ] field-sensitive state
```

## M-MEM-3 — Drop Correctness

```text
[ ] drop flags
[ ] dropck
[ ] aggregate order
```

## M-MEM-4 — Reborrowing

```text
[ ] mutable reborrow
[ ] shared reborrow
[ ] suspension/resume
```

## M-MEM-5 — Lifetime Model

```text
[ ] NLL
[ ] provenance
[ ] returned borrows
[ ] lifetime subtyping
```

## M-MEM-6 — Reference Semantics

```text
[ ] non-null
[ ] alignment
[ ] validity
[ ] provenance
[ ] raw boundary
```

## M-MEM-7 — Interior Mutability

```text
[ ] explicit primitive
[ ] atomic integration
[ ] optimizer integration
```

## M-MEM-8 — Type Capability Model

```text
[ ] Copy
[ ] move-only
[ ] Clone model
[ ] variance
```

## M-MEM-9 — Unsafe Soundness Boundary

```text
[ ] alias contract
[ ] FFI lifetime contract
[ ] callback lifetime model
```

## M-MEM-10 — Concurrency Integration

```text
[ ] scope-bounded borrows
[ ] Send/Sync interaction
[ ] cross-task lifetime checking
```

---

# 41. Production Acceptance Criteria

Crug may claim Rust-grade ownership memory safety for its supported safe subset only when:

```text
[ ] place model is field-sensitive
[ ] partial moves are sound
[ ] partial initialization is sound
[ ] drop flags are correct
[ ] drop checking is correct
[ ] reborrowing is sound
[ ] disjoint-place borrowing is supported or conservatively rejected
[ ] NLL is sound
[ ] lifetime provenance is tracked
[ ] lifetime subtyping is specified
[ ] variance is specified
[ ] reference validity is specified
[ ] pointer provenance is specified
[ ] interior mutability is explicit
[ ] Copy/move-only semantics are specified
[ ] unsafe alias obligations are specified
[ ] pattern matching respects ownership
[ ] aggregate destruction order is frozen
[ ] union ownership rules are safe
[ ] callback/FFI lifetime rules are safe
[ ] concurrency borrow rules are safe
[ ] optimizer respects all of the above
[ ] full regression/fuzz suites pass
```

---

# 42. Intentional Differences from Rust

Crug SHOULD remain simpler where possible.

The following are intentionally not required for Crug 1.0:

```text
full explicit lifetime syntax
two-phase borrows
Rust Pin
full DST system
Rust async/await ownership semantics
closure capture complexity
trait-object lifetime machinery
procedural macro interaction
```

Crug may reject code Rust accepts if:

```text
the rejection is deterministic
the behavior is documented
memory safety remains sound
```

---

# 43. Recommended RFC Split

This document should eventually feed into focused RFCs:

```text
CRUG-RFC-0002  Ownership, Move Paths, Partial Moves, and Drop
CRUG-RFC-0003  Borrowing, Reborrowing, Lifetimes, and Variance
CRUG-RFC-0004  Reference Validity, Pointer Provenance, and Unsafe
CRUG-RFC-0005  Interior Mutability and Atomic Interaction
CRUG-RFC-0006  FFI Lifetime and Callback Contracts
CRUG-RFC-0007  Concurrency Borrowing and Send/Sync
```

Numbering should be reconciled with the broader Crug RFC registry.

---

# 44. Priority Order

Recommended implementation order:

```text
1. Place model
2. Move paths
3. Partial initialization
4. Drop flags
5. Partial moves
6. Reborrowing
7. Disjoint borrowing
8. NLL
9. Lifetime provenance
10. Drop checking
11. Reference validity
12. Pointer provenance
13. Interior mutability
14. Copy/Clone/move-only
15. Variance
16. FFI lifetimes
17. Callback lifetimes
18. Concurrency borrowing
19. optimizer integration
20. production fuzzing/qualification
```

This order minimizes the risk of building later safety features on an incomplete memory model.

---

# 45. Final Target

Crug does not need to be Rust.

It needs to provide a memory model that is:

```text
sound
predictable
C-compatible where intended
zero-runtime where promised
optimizable
portable
testable
```

The final target is:

> **100% of the memory-safety guarantees required by Crug's supported safe feature set, not 100% replication of Rust syntax or language complexity.**
