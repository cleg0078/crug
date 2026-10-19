# Safe core types: `slice`, `option`, `result` and payload enums

Status: **Phase 10 implemented** (2026-10-19), in three increments:
increment 1 (`slice<T>`, `option<T>`, `result<T,E>`, the value constructors,
`match` over both sums, checked slice indexing); increment 2a (payload enums
/ 10D, `match` over them); increment 2b (owned payloads, conditional drop of
the active variant, ownership transfer into a `match` binding). Section 10
lists what is still open; the Phase 10 gate in `EXIT-GATES.md` (amendments
A-11, A-12, A-13) names the checks that close it.

The normative surface is ADR 0018 D7 and D8; the layouts are frozen in
`ABI.md` section 3. This page is the digest: it records what parses, what
lowers, and what a phase must not assume.

## 1. What is implemented

| Construct | Syntax | Flag |
| --- | --- | --- |
| Slice type | `slice<T>` in type position | `-fcrug-slices` |
| Option type | `option<T>` in type position | `-fcrug-result` (implies `-fcrug-adt`) |
| Result type | `result<T,E>` in type position | `-fcrug-result` (implies `-fcrug-adt`) |
| Slice value | `slice (pointer, length)` | `-fcrug-slices` |
| Option value | `some (x)`, `none` | `-fcrug-result` |
| Result value | `ok (x)`, `err (e)` | `-fcrug-result` |
| Match statement | `match (expr) { pattern => body, ... }` | `-fcrug-adt` after closure |
| Payload enum type | `enum Message { Text (char *), Quit };` | `-fcrug-adt` |
| Payload variant value | `Text (s)` | `-fcrug-adt` |
| Unit variant value | `Quit` (the bare enumerator) | `-fcrug-adt` |
| Enum tag reference | `enum Message` in type position | `-fcrug-adt` |
| Owned payload enum | `enum Msg { Text (own char *), Quit };` | `-fcrug-adt -fcrug-ownership` |
| Owned payload transfer | `Text (adopt (p, free))`, `match (move (m)) { Text (v) => ... }` | `-fcrug-adt -fcrug-ownership` |
| Checked indexing | `s[i]` on a `slice<T>` value | `-fcrug-bounds-checks` (default on) |

Nothing here needs a runtime, a helper function or a new GIMPLE code. The types
are ordinary `RECORD_TYPE`s and `UNION_TYPE`s built by the C front end, and
every value constructor is an aggregate initializer (`CONSTRUCTOR` inside a
`COMPOUND_LITERAL_EXPR`), so lowering is plain stores - the same object code a
hand-written C struct with the same layout produces.

## 2. Layouts (frozen, x86_64-pc-linux-gnu)

The layouts are asserted by `gcc/testsuite/crug.dg/safe-types/layout.c` and
recorded in `ABI.md` section 3. Field names are part of the surface: a
program may write `s.len`, and `match` reads the tag field.

| Type | Size | Align | Fields |
| --- | --- | --- | --- |
| `slice<T>` | 16 | 8 | `T *ptr` at 0, `size_t len` at 8 |
| `option<int>` | 8 | 4 | `_Bool some` at 0, `int value` at 4 |
| `option<char>` | 2 | 1 | tag at 0, value at 1 |
| `option<long>` | 16 | 8 | tag at 0, value at 8 |
| `result<int,int>` | 8 | 4 | `_Bool ok` at 0, union `u` at 4 |
| `result<char,long>` | 16 | 8 | tag at 0, union at 8 (aligned to `long`) |
| `enum Message { Text (char *), Image (char *), Quit }` | 16 | 8 | `unsigned int tag` at 0, union `u` at 8 |

The internal names are frozen too: `option` has fields `some` and `value`;
`result` has fields `ok` and `u`, where `u` is a union with members
`value` (the ok payload) and `error` (the err payload); a payload enum has
fields `tag` and `u`, and the union members carry the variant names
(`u.Text`, `u.Image`). Only the selected union member is initialized by a
constructor; reading the inactive member is unspecified, exactly as in C.

An `own` payload changes no layout: the qualifier is analysis metadata and is
erased from the type, so `enum Plain { Text (char *) }` and
`enum Owned { Text (own char *) }` are the same record (`payload-enum-own-abi.c`
asserts it; `ABI.md` section 3 records it).

A generic type with a comma cannot be written as a macro argument: the
preprocessor splits at the comma before the parser sees the type. Typedef the
instantiation, or pass the type through a macro that does not use it as an
argument (`layout.c` typedefs every instantiation for this reason).

## 3. Slices

A slice is a borrowed view: a pointer and a length. It never owns the pointee,
there is no destructor, and no ownership transfer happens at `slice (...)`.

```c
slice<int> s = slice (a, 4);      /* an array decays to a pointer */
slice<int> t = slice (a + 1, 2);  /* an offset pointer keeps its own length */
slice<char> c = slice (b, 3);
int total = 0;
for (size_t i = 0; i < s.len; i++)
  total += s[i];
```

- The first argument must have pointer type after array decay
  (`crug-TYPE-012`); the second is converted to `size_t`.
- Both arguments are evaluated once; `slice (p, n)` is an expression, so it
  can be passed to a function or used as an initializer.
- `slice<T>` is a distinct Crug-native type: it can be a parameter, a return
  type, a local or a member, but it cannot cross a C ABI boundary without the
  two-parameter lowering `(T *ptr, size_t len)` recorded in `ABI.md`
  section 5.

## 4. Option and result

```c
option<int> o = some (7);       /* tag 1, value 7 */
option<int> n = none;           /* tag 0, value unspecified */
result<int,int> r = ok (5);     /* tag 1, u.value 5 */
result<int,int> e = err (7);    /* tag 0, u.error 7 */
```

A constructor takes its sum type from context. The two accepted contexts are a
declaration initializer and a `return` statement; anywhere else `some`,
`ok` or `err` is `crug-ADT-005`. A constructor that does not belong to the
context type (`ok` where an `option` is expected, or `some` where a
`result` is expected) is `crug-ADT-006`. `none` needs an `option` type
and takes no argument.

The tag is a `_Bool`: `0` is the first variant (`none`, `err`) and `1`
is the second (`some`, `ok`), in declaration and match order.

## 5. Payload enums

With `-fcrug-adt` an `enum` definition is a payload enum when at least one
enumerator carries a payload in parentheses (ADR 0018 D8):

```c
enum Message
{
  Text (char *),
  Image (char *),
  Quit,
};

enum Message m = Text ("hi");
m = Quit;
```

- The payload is exactly one type-name. Empty parentheses are `crug-ADT-001`;
  a second type-name after a comma is `crug-ADT-002`; a payload variant used
  without parentheses is `crug-ADT-003`; a unit variant called with
  parentheses is `crug-ADT-004`.
- An enum where no enumerator has a payload stays an ordinary C enum even with
  the flag on (`payload-enum-off.c`), so the flag never changes existing C:
  `enum Color { RED, GREEN };` is still an `ENUMERAL_TYPE` with the C layout.
- A unit variant is constructed by the bare enumerator, a payload variant by
  `Name (value)`. Variant names are ordinary identifiers, so a user function
  or variable with the same name is a C redefinition, and a declared name wins
  over the variant constructor.
- The tag is written `enum Message`, as in C, and follows the tag rules: the
  definition binds the tag in the current scope. A forward `enum M;` followed
  by a payload enum definition is `crug-ADT-017`, because the tag denotes a
  record type and cannot complete an `ENUMERAL_TYPE`; define the payload enum
  before any reference to its tag. An anonymous payload enum is allowed.
- `enum Message : int { ... };` is `crug-ADT-016`: the tag field is part of
  the representation, so a payload enum cannot choose a fixed underlying type.
- Representation: a record of a tag and a union, as in section 2. The tag value
  is the declaration index, 0-based, so the first variant has tag 0. Only the
  tag and the selected union member are initialized; there is no niche
  optimisation and no packing of the tag into padding.
- A payload may be any type-name, including a pointer or an ordinary record.
  With a plain pointer payload the pointer is copied, so the program must
  agree on who frees it; an owned payload (`own char *`) is tracked by the
  ownership pass - see section 6.

## 6. Owned payloads

With `-fcrug-adt -fcrug-ownership` a payload may be an owned pointer
(`own char *`). Ownership is *type-derived*: `own` lives in the declaration
specifiers and is erased from the type, so a payload enum with at least one
owning payload is itself an owning type, and a variable of that type owns the
payload of its active variant.

- Construction transfers ownership: `Text (adopt (p, release))`,
  `Text (move (q))`, `Text (malloc (n))`, or another fresh allocation whose
  destructor is known. A plain borrowed pointer is `crug-ADT-019`: neither the
  constructor nor the analysis would know who frees it.
- A variable of an owning payload enum is destroyed at scope end, on `return`,
  `break`, `continue`, `goto` out of the scope and at a `switch` edge, like
  an `own` pointer (ADR 0004 D4). The drop is
  `if (tag == V) { if (u.Text) release (u.Text); }` for each owning variant
  that can be active - an inactive variant never reaches the call, and a
  variant that owns nothing has no drop at all (`adts-drop.c`, and the tag
  guard is visible in the generated code at `-O0`).
- Copying is `crug-UAM-003` and reading a moved value is `crug-UAM-001`:
  `enum Msg n = m;` is rejected, write `enum Msg n = move (m);`.
- `match` over an owning payload enum requires `match (move (x))`
  (`crug-ADT-015`). The payload is transferred to the arm binding, which owns
  it in the arm body and destroys it at the end of the arm unless it is moved
  out (directly or through a local). An arm for an owning variant must bind the
  payload (`crug-ADT-020`), and a `_` arm may only cover variants that own
  nothing; name the owning variants explicitly so each payload has a binding.
- The value cannot cross a function boundary by value: passing or returning it
  is `crug-ADT-018`, because the payload destructor is not part of the ABI.
  Keep the value in one function, pass a pointer, or move the payload out
  before the call.
- Boundary: ownership is tracked for local variables and `match` bindings. An
  owning payload enum inside an aggregate (a struct field, a union member, an
  array element) is not tracked yet, the same limitation an `own` field has
  since the Phase 4/5 MVP; keep such values in locals.
- `scripts/check-owned-payloads.sh` runs this contract end to end: one drop
  for the active variant, none for an inactive one, a tag guard before the
  destructor call, and the six unsafe forms rejected with their codes.

## 7. Match

```c
match (lookup (3))
  {
  ok (o) => {
    match (o)
      {
      some (v) => { got = v; }
      none     => { bad = 1; }
      }
  }
  err (e) => { bad = e; }
  }
```

- The scrutinee is evaluated once into an artificial temporary.
- An arm is `pattern => expression;` or `pattern => { ... }`, separated by
  an optional comma; a payload binding is a plain name in parentheses, and its
  type is the payload type of that variant.
- A pattern is a variant of the scrutinee type: `some`/`none` for an
  `option`, `ok`/`err` for a `result`, and the enumerator names for a payload
  enum, in any order. A unit variant takes no payload: `none (v)` and
  `Quit (v)` are `crug-ADT-004`.
- `_` is a wildcard arm and must be last; a missing variant without `_` is
  `crug-ADT-011` naming the first missing variant; a duplicate arm is
  `crug-ADT-012`; an unknown pattern is `crug-ADT-010`.
- `match` is a statement, not an expression, and the scrutinee must be an
  `option`, `result` or payload enum value (`crug-ADT-013`); exhaustiveness
  covers every variant, unit variants included. The expression form is not
  accepted yet: `crug-ADT-014` is registered but the recovery path that emits
  it is still to be written, so `x = match (...)` is a plain parse error today.
- Lowering is a chain of void `COND_EXPR`s testing the tag, with the payload
  binding assigned from the selected field inside its own scope; there is no
  jump table and no helper.
- An owning payload enum must be moved into `match` (`crug-ADT-015`); the
  payload then belongs to the arm binding - see section 6.

## 8. Checked indexing

With `-fcrug-bounds-checks` (the default), `s[i]` lowers so that an
out-of-range index traps at the point of the access:

```
((size_t) i >= s.len) ? __builtin_trap () : ...   in the pointer operand of
the array reference, with the index held in a SAVE_EXPR
```

The comparison is unsigned, so a negative index traps as well. The trap sits in
the pointer operand, so `s[i]` is still an lvalue and `s[i] = v` works. The
index is evaluated exactly once even though it appears in both the check and
the reference.

`-fno-crug-bounds-checks` removes the comparison and the trap: `s[i]` becomes
a plain pointer addition over `s.ptr`, with no `ud2` in the object. The flag
is a C-language option (`gcc/c-family/c.opt`), like the other Crug front-end
switches; it changes only this lowering.

The trap is `__builtin_trap`, so a violating program dies with `SIGILL`
(exit status 132 on the shell) rather than returning a wrong value.
`scripts/check-safe-bounds.sh` asserts both the trap and its absence.

## 9. Gating and identity

- Every production needs its flag: with no `-fcrug-*` option the words are
  ordinary identifiers and a generic type is a plain-C syntax error
  (`flags-off.c`); `some` and `none` still name user functions and variables
  (`shadowing.c`).
- `-fcrug-result` implies `-fcrug-adt` through the Phase 3 closure, so
  `match` is available with `-fcrug-result` alone.
- `-fcrug-slices`, `-fcrug-result`, `-fcrug-ownership` and
  `-fcrug-bounds-checks` are described in `FLAGS.md`. An owned payload needs
  `-fcrug-ownership` as well as `-fcrug-adt`: without it `own` is an unknown
  type name inside the payload parentheses.

## 10. What is not implemented yet

- No expression form of `match` (`crug-ADT-014` is registered; the parser has
  no recovery path for it yet), and patterns cover variants only - no integer,
  string or range patterns, no multi-payload patterns, no qualified variants.
- Payload ownership is per value, not per field: an owning payload enum inside
  a struct, union or array is not tracked (section 6, boundary).
- Owned payloads in `option<T>` and `result<T,E>` are not tracked either:
  increment 2b implements them for payload enums only. Storing an `own`
  pointer in an `option` payload copies the pointer, so the program must agree
  on who frees it.
- No `?` operator, no niche optimisation, no generic user types, no
  traits (post-1.0 per MASTER_SPEC section 32), no mutable-slice distinction
  (`mut slice<T>` is not defined yet), no pattern matching on integers or
  strings.
- No ABI stability beyond `ABI.md` section 3 and section 6: the four frozen
  rows are version 1, and changing any recorded size, alignment or field order
  is an ABI change.

## 11. Tests

| Check | What it proves |
| --- | --- |
| `make check-SAFE_TYPES` | the 39 fixtures under `gcc/testsuite/crug.dg/safe-types/` (plus the three under `bounds/` that the script drives): 77 expected passes, 0 failures |
| `scripts/check-safe-types.sh` | runs `make check-SAFE_TYPES` and tallies PASS against FAIL/XPASS/UNRESOLVED/ERROR from `gcc.sum`; prints `safe-types: 77 tests, 0 failures` |
| `scripts/check-owned-payloads.sh` | the owning payload contract of section 6: one drop for the active variant, none for an inactive one, a tag guard in the generated code, and the six unsafe forms rejected with `crug-ADT-018`, `crug-ADT-019`, `crug-ADT-020` and `crug-UAM-003` |
| `scripts/check-safe-bounds.sh` | in-range indexing exits 0, an out-of-range index traps with `SIGILL`, the checked object contains at least one `ud2` and the unchecked one none, and a plain C array is never trapped |
| `gcc/testsuite/crug.dg/safe-types/layout.c` | every size, alignment and offset in `ABI.md` section 3 as `_Static_assert`s |
| `make check-C_COMPAT` | plain C and the 30-program corpus are unaffected |

Run cases are `slice-run.c`, `option-run.c`, `result-run.c`,
`payload-enum-run.c`, `payload-enum-tag.c`, `payload-enum-own-run.c`,
`payload-enum-own-match.c`, `payload-enum-own-move.c`, `adts-drop.c`,
`bounds-off.c` and `shadowing.c`; rejection cases are
`slice-non-pointer.c`, `slice-arity.c`, `result-arity.c`,
`generic-unclosed.c`, `ctor-needs-context.c`, `ctor-wrong-type.c`,
`none-payload.c`, `match-non-exhaustive.c`, `match-not-sum.c`,
`payload-enum-empty-payload.c`, `payload-enum-two-types.c`,
`payload-enum-needs-payload.c`, `payload-enum-unit-payload.c`,
`payload-enum-exhaustive.c`, `payload-enum-fixed-underlying.c`,
`payload-enum-forward.c`, `payload-enum-not-sum.c`,
`payload-enum-off.c`, `payload-enum-own-bare.c`,
`payload-enum-own-copy.c`, `payload-enum-own-scrutinee.c`,
`payload-enum-own-unbound.c`, `payload-enum-own-wildcard.c`,
`payload-enum-own-param.c` and `flags-off.c`. The compile cases `layout.c`,
`payload-enum-layout.c` and `payload-enum-own-abi.c` assert the frozen
layouts.
