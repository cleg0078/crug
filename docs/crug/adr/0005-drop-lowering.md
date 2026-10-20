# ADR 0005 - Drop Lowering: Destruction and Non-Local Control Flow

- **Status:** Accepted (2026-10-18)
- **Ratified:** 2026-10-18 by CLEG0078 (project owner)
- **Date:** 2026-10-18
- **Deciders:** project owner (ratification), frontend + ownership pass owners
- **Context docs:** RFC-0001 sections 19, 20 and 42; MASTER_SPEC s14.8;
  RUST_MEMORY_GAPS s7, s22

## Context

The spec promises deterministic destruction but never says where in the pipeline
it happens, what syntax attaches a destructor, or what happens on `goto`,
`switch`, `setjmp`/`longjmp`, `exit`, `fork`, thread cancellation and signal
return. RFC-0001 section 19 turns this into an escape: the compiler "MUST NOT claim
deterministic cleanup through abnormal control flow it does not model". Without a
defined model, every implementation gets to choose, and the O1 conformance class
becomes unmeasurable. This ADR defines the model.

## Decision

### D1 - Drop elaboration is a CFG transformation after the ownership pass

Pipeline position (naming per GAPS s34): ownership dataflow
(`crug-ownership`) -> drop insertion (`crug-drop-elab`) -> gimplification. Drop
elaboration runs on the CFG of the function body as seen by the frontend, before
any optimiser pass, so that every jump edge is explicit. Inserted drop statements
carry the source location of the declaration whose scope they close.

### D2 - Destructor attachment syntax

Two spellings, both already implied by the spec, now made explicit:

- `adopt(expr, dtor)` - attaches `void (*)(T *)` at the point ownership is
  created (postfix intrinsic, see ADR 0002 D6);
- `__attribute__((crug_dtor(fn)))` on a struct field - default destructor for
  values of that field type, so wrapper types do not need `adopt` at every
  construction.

`drop(expr)` is the explicit early drop. `release(expr)` abandons without
dropping. `move(expr)` transfers. That is the complete destructor vocabulary;
nothing else is implicit.

### D3 - Structured control flow

- **return**: all owned variables still OWNED are dropped in reverse declaration
  order on the return edge, except the returned value (which is moved out).
- **break / continue**: drop every owned variable declared in the scopes being
  exited, at the edge, before the jump.
- **switch**: same as break for the exited scope; fallthrough is not a jump and
  drops nothing.
- **goto that leaves scopes**: legal; the drop sequence for the exited scopes is
  duplicated on the edge. This can generate several copies of the same call
  sequence; the optimiser may merge them later (`-ftree-tail-merge` is not
  required for correctness).
- **goto into a scope with owned declarations**: rejected
  (`error: jump into scope of 'x' with automatic ownership`). No drop state
  exists on entry, so no semantics can be assigned.

### D4 - setjmp / longjmp

The frontend cannot intercept `longjmp`; therefore the language defines a domain:

- `-fcrug-strict`: it is an error to call `setjmp` in a function that has an
  owned value live across the `setjmp` call (`error: owned value 'x' is live
  across setjmp; longjmp would skip its destructor`).
- `-fcrug-ownership` (non-strict): same condition is
  `-Wcrug-setjmp-drops`, documented as "destructors between `setjmp` and
  `longjmp` are not run".
- If the `setjmp` buffer escapes the function or is passed to `longjmp` from
  unknown code, the compiler cannot see it; that is the documented boundary and is
  what section 19 permits.

The rule is therefore: Crug never claims cleanup across `longjmp`; it rejects the
statically visible cases under strict and warns otherwise.

### D5 - process lifetime (`exit`, `_Exit`, `abort`)

`exit` and `_Exit` do not run destructors; `abort` does not either. This matches
C and is stated in LANGUAGE.md. A destructor is never registered as an `atexit`
handler; doing so would add hidden runtime and change ordering.

### D6 - Signals, fork, exec, threads

- A destructor is not async-signal-safe by default; calling `drop` from a signal
  handler is undefined unless the destructor is documented as such.
- `fork` duplicates owned values; the child owns its copies. No destructor runs
  at fork.
- `exec` discards all owned values; destructors are not run (process image
  replacement cannot run user code). This must be documented: `exec` leaks
  everything, by definition.
- Thread cancellation and `pthread_exit`: destructors owned by the cancelled
  thread are not run unless the runtime task layer is used (see RFC-0003 work);
  Crug does not intercept cancellation.

### D7 - Task scopes

When `scope` (MASTER_SPEC s17) is used, child tasks own their captures; a task's
destructors run at the end of the task body, and the scope does not complete until
children complete (RFC-0001 section 29). A captured owned value is moved into the child;
the parent may not drop it. If a task is cancelled before completion, its owned
values are dropped at the cancellation point in reverse order - this is the only
asynchronous drop in the language, and it is bounded by "the task body ends here".

### D8 - Diagnostics

- `-Wcrug-drop-unreachable`: an owned value can never be dropped on any path
  (silent leak) - warning in hybrid, error in strict.
- `-Wcrug-setjmp-drops`, `-Wcrug-owned-byte-copy` (ADR 0004 D9),
  `-Wcrug-unadopted-own`.
- Every drop-site diagnostic quotes both the declaration location and the jump
  location, so that the fix is unambiguous.

## Consequences

- The O1 claim ("deterministic drop for supported control flow") now has a precise
  meaning: the supported control flow is D3+D7; D4-D6 are documented non-claims,
  and strict mode rejects the visible parts.
- Drop elaboration is a pure CFG rewrite, testable with `-fdump-tree-crug-drop`.
- The `goto` duplication may slightly increase code size at `-O0`; accepted,
  because correctness at `-O0` is the point of the feature.
- Tests: `crug.dg/drop/*` (return, break, continue, switch, goto out, goto in
  rejected, setjmp strict error, exit does not run dtor, task scope ordering,
  nested aggregates order).

## Alternatives rejected

- **Model `longjmp` by instrumenting `setjmp`.** Requires a runtime frame chain
  and unwinding support; violates zero-cost and would not cover `longjmp` from
  uninstrumented C frames.
- **Prevent all `setjmp` in Crug code.** Too restrictive: legitimate signal-safe
  C code uses it, and the C0 compatibility class must keep compiling.
- **Drop at every basic-block exit unconditionally.** Double drops on paths where
  the value was moved; ADR 0003 D2 makes the join a compile error instead.

## Amendments

The decisions above are unchanged in intent. Phase 5 implemented them, and the
implementation forced the following clarifications. Each one is binding.

### A-1 - An `own` parameter is owned by the callee

D2 does not say who owns a parameter marked `own`. A-1 fixes it: the callee owns
it, and destroys it on every exit after its own locals (locals first, then
parameters, each class in reverse declaration order). The caller is not checked:
it may pass a value it still owns, and it may pass a temporary. That gap is
deliberate (call-site checking arrived with Phase 8, and only under
`-fcrug-strict`) and is recorded in `DROP.md` section 8.

**Implementation note (Phase 23).** The parameter starts `OWNED` with no
destructor (`gcc/c/crug-ownership.cc`, `crug_check_function`): the compiler
never invents one for a parameter, so nothing is destroyed unless the callee
transfers the value into an owned declaration that has a destructor. A
parameter that is neither adopted nor released warns `crug-OWN-007` and emits
no call. `DROP.md` section 4 states the rule as implemented.

### A-2 - Destructor metadata lives in the analysis environment only

D2's `__attribute__((crug_dtor(fn)))` spelling is **not implemented** in Phase 5;
the attribute is still accepted as an ordinary ignored attribute and does
nothing. A destructor enters the model in exactly two ways: `adopt (p, d)`, and
the allocation catalogue (A-3). The destructor is carried as a field of the
analysis state (`{state, dtor}`), never as a `TYPE_*`, `DECL_*` field, side
table, drop flag or runtime value - ADR 0003 D1/D5. `move` transfers the
destructor with the value; `release` and `drop` discard it.

### A-3 - Allocation catalogue

A pointer returned by `malloc`, `calloc`, `realloc`, `reallocarray`, `strdup`,
`strndup` or `aligned_alloc` (and their `__builtin_` spellings) is born OWNED.
Its destructor is `free` if - and only if - the translation unit has a visible
declaration of `free` as a function; otherwise the value is OWNED with no known
destructor and `-Wcrug-unadopted-own` warns at scope exit. The catalogue is a
frontend fact about libc, not a language rule, and no header is included on the
user's behalf.

### A-4 - Assigning over an owned value is an error

D3 says nothing about `p = q;` when `p` still owns something: the old value
would leak. That is `crug-DROP-008` ("assignment to an owned value that already
owns; call `drop()` or `release()` first"). It is an error, not a warning,
because the leak is unconditional. After reporting it the analysis moves the
variable to MOVED with no destructor, so one mistake does not cascade into an
`OWN-007` at scope end.

### A-5 - `EMPTY`: an owned pointer that owns nothing

D3's state set had no way to express "declared but not yet owning". Phase 5 adds
`EMPTY`: `own T *p;` and `own T *p = 0;` are both EMPTY (own nothing, drop
nothing, no warning). This is what makes early returns and conditional ownership
usable. `OWNED` with `EMPTY` at a join becomes `MAYBE_EMPTY`; destroying a
MAYBE_EMPTY value is `crug-DROP-007` ("may be used before it is initialized"),
reported at the declaration with a note at the exit.

### A-6 - The state lattice and the join are normative

| A \ B | EMPTY | OWNED(d) | MOVED | MAYBE_MOVED | MAYBE_EMPTY |
| --- | --- | --- | --- | --- | --- |
| EMPTY | EMPTY | MAYBE_EMPTY | EMPTY | MAYBE_MOVED | MAYBE_EMPTY |
| OWNED(d) | MAYBE_EMPTY | OWNED(d) or OWN-010 if dtors differ | MAYBE_MOVED | MAYBE_MOVED | MAYBE_EMPTY |
| MOVED | EMPTY | MAYBE_MOVED | MOVED | MAYBE_MOVED | MAYBE_MOVED |
| MAYBE_MOVED | MAYBE_MOVED | MAYBE_MOVED | MAYBE_MOVED | MAYBE_MOVED | MAYBE_MOVED |
| MAYBE_EMPTY | MAYBE_EMPTY | MAYBE_EMPTY | MAYBE_MOVED | MAYBE_MOVED | MAYBE_EMPTY |

Two rules complete it. (i) A join that cannot prove single destruction is
`crug-DROP-006`, reported **once per declaration** (at the declaration, with a
note at the join), so that a later correction does not move the diagnostic. (ii)
If a MAYBE_* value has no known destructor, the analysis warns `crug-OWN-007`
instead of `crug-DROP-006`: there is no destructor to run twice or to skip, so
the conservative rejection would reject programs that are in fact fine (for
example an `own` value passed to a function that frees it).

### A-7 - Diagnostics: option plus code, and the kernel conflict

D8 lists options only. Every diagnostic now also carries a stable code from
the registry in `docs/crug/DIAGNOSTICS.md`, in addition to the `-Wcrug-*`
option. Warnings print both (`error: msg [-Wcrug-x] [crug-AREA-NNN]`,
promoted to errors by `-fcrug-strict`); always-error diagnostics print the code
with no option annotation, because GCC's `error_at` takes no option argument.
Finally, D1's dump option is spelled `-fcrug-dump=drop` (not
`-fdump-tree-crug-drop`), and `-fcrug-kernel` rejects `-fcrug-ownership` and
`-fcrug-borrow`. A-7 left that conflict open for the kernel ADRs; Phase 19
answered it: kernel code carries no ownership analysis and no drop lowering in
v1 (ADR 0017 D1), so the rejection is policy, not a phase that is still
coming.

