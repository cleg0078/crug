# Crug ownership MVP: @own@ and @move@

Status: **Phase 4 implemented** (2026-10-19). @own@ and @move@ are accepted
under @-fcrug-ownership@, the analysis rejects the four MVP misuse classes, and
the post-join state is dumpable. Drop insertion, borrow checking and destructors
do not exist yet. The normative text is ADR 0003 (metadata storage), ADR 0004
(analysis layer), ADR 0018 D2/D3/D9 (surface grammar and diagnostics) and
RFC-0001 sections 13-19; the gate is @EXIT-GATES.md@ phase 4.

## 1. What is accepted

| Form | Meaning | Notes |
| --- | --- | --- |
| @own T *p;@ | @p@ owns one @T@ object | declaration specifier, erased from the type |
| @own T **p;@ | @p@ owns one pointer-sized object | the outermost pointer level owns |
| @own T a[4];@ | four owned @T@ objects, dropped 3..0 later | array declarator: the element type owns |
| @own@ struct member | the containing object owns the member | dropped with the aggregate (Phase 5) |
| @own@ parameter | erased at the call boundary | ADR 0003 D3; not tracked inside the caller |
| @own@ return type | erased at the boundary | same |
| @typedef T *P; own P p;@ | @p@ owns one @T@ | typedef cannot carry the qualifier |
| @move (e);@ | transfers the owning value | @e@ is an owning lvalue or owned prvalue |

The qualifier is a declaration specifier, so @own int i;@, @own T (*fp)(void);@,
a bit-field, a flexible array member and an unnamed member are rejected
(@crug-TYPE-001@, @crug-TYPE-006@). An owning member in a union is rejected when
the union is completed (@crug-OWN-009@, ADR 0004 D6): the MVP has no
active-variant contract, so a union of owning values has no defined destruction.

@move@ is not a keyword and not a runtime helper. It is the identifier @move@ in
callee position with no visible declaration and the feature on
(@__builtin_crug_move@ internally). A user declaration named @move@ wins, which
keeps every existing C program valid. The call is replaced by its operand, so
the object code contains a register copy and no call.

## 2. The state machine

Each tracked object is in one of four states:

| State | Meaning |
| --- | --- |
| @UNINIT@ | declared, not yet assigned an owned value |
| @OWNED@ | holds the responsibility to destroy the object |
| @MOVED@ | transferred away; using it is an error |
| @MAYBE_MOVED@ | the branches disagree; using it is an error |

The tracked set for the MVP is every declaration whose specifiers carry
@own@, at variable granularity. Assigning an owned value (an @own@
declaration with an initializer, @move (p)@, @adopt@ in a later phase) makes the
target @OWNED@; @move@ makes the source @MOVED@.

At a control-flow join the analysis merges the incoming states. Equal states
stay; @UNINIT@ with @OWNED@ stays @UNINIT@ (the value may be uninitialised, so it
is not used and not dropped); every other disagreement becomes @MAYBE_MOVED@.
ADR 0003 D2 is explicit that the join must not pick a side: an object that is
owned on one path and moved on another has no static answer, and the MVP rejects
using it rather than guessing.

The states are static facts stored in the frontend analysis, never in the object
or in a side table (ADR 0003 D1, D5). No hidden field, bit, reference count,
registry or helper call is added, so @sizeof@, alignment, layout and calling
representation are identical to plain C. The phase-4 gate checks that identity
twice: @abi-ptr.c@ asserts the sizes and the object carries no @__crug_@
symbol, and @move-zero-cost.c@ disassembles to zero calls.

## 3. Diagnostics

All are errors in the MVP; the @-Wcrug-*@ spelling is the option that Phase 3's
option table will use once the warnings become opt-in, and every message carries
a stable code from @docs/crug/DIAGNOSTICS.md@.

| Code | Trigger |
| --- | --- |
| @crug-UAM-001@ | use of a moved value |
| @crug-UAM-002@ | move of an already-moved value |
| @crug-UAM-003@ | implicit copy of an owned value (owned values are not copyable) |
| @crug-UAM-004@ | use of a possibly-moved value after a branch join |
| @crug-UAM-006@ | move of an aggregate field is not supported in the MVP |
| @crug-OWN-001@ | move operand is not an owned object |
| @crug-OWN-009@ | owning member in a union |
| @crug-TYPE-001@ | qualifier on a declarator that owns nothing (for example @int@) |
| @crug-TYPE-002@ | more than one Crug qualifier on one declarator |
| @crug-TYPE-004@ | @_Atomic@ combined with a Crug qualifier |
| @crug-TYPE-005@ | owning function pointer type |
| @crug-TYPE-006@ | qualifier on a bit-field, flexible array member or unnamed member |

The message format is the one the gate greps:
@file:line:col: error: <message> [crug-AREA-NNN]@. The area-code registry and
@scripts/check-diagnostics.sh@ make the codes unrenumberable.

## 4. The ownership dump

@-fcrug-dump=ownership@ writes @<stem>.crug-ownership@ next to the compiler's
working directory. The first line is @crug ownership dump v1@; each join prints
one line:

@join <file>:<line>: <decl>=<A>/<B> -> <RESULT>@

For a variable owned on one branch and moved on the other:

@join branch-join.c:16: p=OWNED/MOVED -> MAYBE_MOVED@

The phase-4 gate compiles @branch-join.c@ with the dump on and greps that line.
An unknown dump kind is rejected (@unrecognized argument to -fcrug-dump=@), and
the option changes no object code.

## 5. What is deliberately absent

| Missing | Phase |
| --- | --- |
| drop insertion and destructors (only the analysis states exist) | 5 |
| @adopt@ / @release@ / @drop@ intrinsics | 5 |
| borrow checking (@ref@, @mut@) and reborrowing | 6 |
| partial moves (@move (s.field)@); rejected with @crug-UAM-006@ | post-MVP, ADR 0004 D4 |
| cross-function ownership transfer beyond the ABI erasure rule | later |
| kernel-mode ownership restrictions | Phase 19 (optional) |
| @-Wcrug-unadopted-own@, @-Wcrug-ffi-ownership@ and the other warnings | when the option layer lands |

Consequences worth knowing: an owning aggregate is non-copyable, so
@own struct Foo b = *a;@ is rejected; the way to transfer is @move@; and
because no drop is inserted yet, an owning object that is not moved simply leaks
in this phase. That is intentional - Phase 5 adds destruction, and the analysis
is written so that drop decisions are already path facts.

## 6. Tests

| Check | What it proves |
| --- | --- |
| @make check-OWNERSHIP@ / @gcc/testsuite/crug.dg/crug.exp@ | 17 expected passes over the @ownership/@ directory |
| @abi-ptr.c@ | @sizeof@ / @_Alignof@ identity and no @__crug_@ symbol |
| @move-zero-cost.c@ | @move@ lowers to a register copy, no helper call |
| @use-after-move.c@, @double-move.c@, @implicit-copy.c@ | the three rejection classes, each as a @dg-error@ |
| @maybe-moved.c@ | a join of owned and moved rejects the later use (@crug-UAM-004@) |
| @field-move.c@ | partial moves are rejected (@crug-UAM-006@) |
| @own-union.c@, @own-flexible-array.c@ | union and flexible-array member rejections |
| @branch-join.c@ | the dump prints @p=OWNED/MOVED -> MAYBE_MOVED@ |

The phase-4 gate runs the dg suite, the two ABI checks, the diagnostic-code
check and the dump check; @scripts/gate.sh --run phase-4@ archives the output.

## 7. Adding a tracked place

The pass lives in @gcc/c/crug-ownership.cc@ and is called once per function
definition from @c_parser_declaration_or_fndef@. A new tracked place needs:
@crug_mark_owning_decl@ at the declaration site, a case in @walk_expr@ for the
statement that assigns or moves it, and a @dg-error@ test in
@gcc/testsuite/crug.dg/ownership/@. Any change to the state lattice or to the
join rule is an ADR 0004 amendment, not a local edit.
