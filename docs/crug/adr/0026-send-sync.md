# ADR 0026 - Send and Sync: what may cross a worker boundary

- **Status:** Accepted
- **Date:** 2026-10-20
- **Deciders:** project owner (the property names and the escape hatch),
  compiler maintainer (the structural inference), runtime maintainer (the
  channel and task sides of the rule)
- **Relates:** ADR 0004 (ownership), ADR 0007 (FFI metadata),
  ADR 0008 (goroutine stack model), ADR 0009 (scheduler model),
  ADR 0018 D4 and D6 (the `go` and channel grammar), ADR 0024 (structured
  scope), ADR 0025 (channels), MASTER_SPEC section 28,
  CRUG_PRODUCTION_ROADMAP section 22, `docs/crug/SEND-SYNC.md`

## Context

Ownership answers who releases a value. It does not answer who may *touch* it.
A copied integer is harmless on another worker; a copied pointer gives two
tasks the same object with no rule about concurrent access, which is how a
data race becomes a use-after-free. The concurrency surface added in Phases
14-16 (`go`, `scope`, `chan T`) can carry any value the type system allows, so
without a property on the carried type the ownership model appears to bless
exactly the programs it cannot reason about.

MASTER_SPEC section 28 names the two properties and fixes one boundary in
advance: "Do not assume raw pointers are automatically safe." RFC-0001 does
not give Crug traits, generic user types or dynamic guards, so the property has
to be inferred structurally from the C types the program already has, and the
only place a program can state a fact the compiler cannot verify is the FFI
sidecar (ADR 0007) - which already has `send`/`!send`/`sync`/`!sync` on type
declarations and `thread_affinity` on functions, recorded but not yet enforced
("enforcement belongs to P17+", ADR 0007).

## Decision

### D1 - The checks switch on with the features that make them load-bearing

There is no `-fcrug-send-sync` flag. The inference and the diagnostics are
active when `-fcrug-goroutines` or `-fcrug-channels` is enabled, because only a
task or a channel can carry a value to another worker. A single-threaded
translation unit keeps plain C semantics; `-fcrug-goroutines` alone is enough
for `go`, and `-fcrug-channels` alone is enough for a channel that carries a
value.

### D2 - Send: a value may be copied to another worker

`Send` is inferred structurally:

- integer, floating, complex, vector, offset and function types are `Send`;
- a pointer to a function is `Send`; a channel handle is `Send`;
- an array is `Send` when its element is;
- a record or union is `Send` when every field is; an incomplete type is not;
- an owning pointer (`own T *`) is `Send` when `T` is, because `move ()`
  transfers it and the receiver becomes the owner;
- any other pointer is **not** `Send`: a raw pointer names an object without
  saying who may touch it, and the copy would share the pointee.

A value whose type is not `Send` cannot be captured by `go` or sent on a
channel (`crug-CONC-024`). The rule is deliberately conservative: a pointer
inside a struct makes the struct non-`Send` even in a program where the pointee
happens not to be shared, because the compiler cannot check the happens-before
edge.

### D3 - Sync: a shared borrow may be read concurrently

`Sync` uses the same structural base set. A **mutable** borrow captured by a
scoped task requires the pointee to be `Send` (the task is the only writer, and
the value must be safe to reach from another worker); a **shared** borrow
requires the pointee to be `Sync` (`crug-CONC-026`). Outside a `scope` the older
rule still applies unchanged: a borrow cannot be captured at all
(`crug-CONC-012`), because an unstructured task may outlive the borrow.

Two children of one scope may not hold a mutable borrow and any other borrow of
the same lender; that is the scope rule of ADR 0024, reported with the borrow
codes BORROW-002 and BORROW-003.

### D4 - Channels carry Send values and pass ownership explicitly

A channel element type must be `Send` (`crug-CONC-027`), and a value sent on a
channel must be `Send` (`crug-CONC-024`). An owning element is transferred:
the send must wrap the value in `move ()` (`crug-CONC-003`), the receiver owns
the value, and an `adopt ()` destructor cannot cross (`crug-CONC-023`). A
`select` arm may not bind a receive from an owning-element channel
(`crug-CONC-017`), because the arm may not run and the value would be lost.

### D5 - Thread affinity is declared by FFI and is not transferable

An FFI function may declare `thread_affinity NAME`. A value initialized
directly from such a call belongs to that thread, and the property travels
through `move`, `borrow` and `borrow_mut` and through `&`; capturing it into a
task or sending it on a channel is `crug-CONC-025`.  An aggregate member that
receives an affine value taints the object that owns it (fix-p3, CONC-02), so
the property cannot be laundered through a plain struct field. The escape is to use the
value on its own thread; a future exclusion or transfer API (post-1.0) is the
only way to lift the property, and it must be an explicit call, never an
annotation on a random pointer.

### D6 - The FFI sidecar is the only widening, and the author owns the claim

`type T send sync;` widens the inference for `T`; `type T !send;` and `!sync`
narrow it. The compiler checks the syntax and the consistency of the
declarations, not the safety claim: the sidecar author asserts that the object
may cross or be shared, and a wrong `send` annotation is a P0 bug in the
sidecar, exactly like a wrong ownership annotation in ADR 0007. A `send`
annotation on a pointer type widens the **pointee**, so `own T *` with an
annotated `T` is accepted. Because one type may be declared once, two lines
that disagree are a sidecar error, not an override.

### D7 - One module enforces both properties at every crossing

The inference and its state live in `gcc/c/crug-send-sync.cc`: the recursive
predicates, the per-function map of affine declarations, and the FFI annotation
lookup. The crossings call into it: `crug_check_go_capture` for `go` (including
the scoped-borrow rules), `crug_channel_type` and `crug_channel_send` for
channels. The ownership pass only records member stores into the affinity
map (`crug_note_affine_store`); the property itself is checked before a
capture is walked, so `move ()` still does the ownership work it did in
Phase 5. Affine declarations are forgotten at the end of the function that
declared them.

### D8 - Limits, stated so they cannot be mistaken for guarantees

- The inference is structural over C types. Crug has no user generics, no
  traits and no negative reasoning, so a user type that wraps a pointer in a
  `uintptr_t` or hides it behind an opaque `void *` is not seen.
- Aggregate owning fields are still untracked by the ownership pass (the
  Phase 5 MVP limit): a struct with an `own` pointer field is `Send` by its
  field types but the copy is not tracked, so a receiver of such a struct must
  not release the field. This is a known residual risk, not a rule the compiler
  enforces.
- There are no dynamic checks, no runtime lock injection, and no kernel-mode
  concurrency model (MASTER_SPEC section 28 leaves kernel concurrency out).
- `memory_order` and the PAL's atomics are not modelled; the property is about
  reachability, not about synchronisation.

## Consequences

- A pointer that crosses a worker boundary now requires either an ownership
  transfer (`own`) or an FFI annotation; the common program that captured an
  `int *` and "worked" is rejected at compile time. The Phase 14 array capture
  (`go fill (buf, 4)`) is such a case: an array decays to a pointer, which is not
  `Send`, so the fixture now expects `crug-CONC-024` and the runtime program
  passes no pointer.
- No ABI changes: the runtime never sees the property, `chan T` is still one
  pointer, and the task frame still holds a copy of the captured values.
- The compiler suite gains `gcc/testsuite/crug.dg/send-sync/`; the runtime side
  is the unchanged task and channel programs plus
  `scripts/check-send-sync.sh`, which also exercises the two sidecar escapes.
- A later phase can add a `Sync`-aware exclusion API or a richer affinity model
  without changing this ADR's inference, because every crossing funnels through
  one module.

## Alternatives considered

- **Rust-style auto traits with a trait system.** Rejected: C has no trait
  machinery or coherence, and MASTER_SPEC section 28 asks for a C-shaped rule,
  not a new type system.
- **Making raw pointers `Send` and relying on the programmer.** Rejected: it
  contradicts MASTER_SPEC section 28 in one sentence and turns the ownership
  model into a false assurance; the FFI annotation gives the same escape with a
  written claim attached.
- **Runtime handles or locks that make any value safe to share.** Rejected as a
  phase-17 non-goal: no dynamic checks and no runtime lock injection.
- **Inferring `!send` from the absence of an annotation on FFI opaque types.**
  Rejected for now: an unannotated type is unknown, so a pointer to it stays
  non-`Send`, which is the conservative reading; the sidecar may widen it.
