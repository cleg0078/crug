# Safe core types: `slice`, `option` and `result`

Status: **Phase 10 increment 1 implemented: `slice<T>`, `option<T>`,
`result<T,E>`, the value constructors, `match` over `option`/`result` and
checked slice indexing** (2026-10-19). Increment 2 adds payload enums (10D) and
owned payloads; section 8 lists what is still open, and the Phase 10 gate in
`EXIT-GATES.md` (amendment A-11) names the checks that only close with it.

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

The internal names are frozen too: `option` has fields `some` and `value`;
`result` has fields `ok` and `u`, where `u` is a union with members
`value` (the ok payload) and `error` (the err payload). Only the selected
union member is initialized by a constructor; reading the inactive member is
unspecified, exactly as in C.

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

## 5. Match

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
- `none` takes no payload: `none (v)` is `crug-ADT-004`.
- `_` is a wildcard arm and must be last; a missing variant without `_` is
  `crug-ADT-011` naming the first missing variant; a duplicate arm is
  `crug-ADT-012`; an unknown pattern is `crug-ADT-010`.
- `match` is a statement, not an expression, and the scrutinee must be an
  `option` or `result` value (`crug-ADT-013`). Payload enums extend this
  when 10D lands.
- Lowering is a chain of void `COND_EXPR`s testing the tag, with the payload
  binding assigned from the selected field inside its own scope; there is no
  jump table and no helper.

## 6. Checked indexing

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

## 7. Gating and identity

- Every production needs its flag: with no `-fcrug-*` option the words are
  ordinary identifiers and a generic type is a plain-C syntax error
  (`flags-off.c`); `some` and `none` still name user functions and variables
  (`shadowing.c`).
- `-fcrug-result` implies `-fcrug-adt` through the Phase 3 closure, so
  `match` is available with `-fcrug-result` alone.
- `-fcrug-slices`, `-fcrug-result` and `-fcrug-bounds-checks` are described
  in `FLAGS.md`.

## 8. What is not implemented yet

- **Payload enums (10D).** `enum` with payload variants, and `match` over
  one, are not parsed yet. The registry rows `crug-ADT-001`..`-003` and the
  `adt` half of the Phase 10 gate stay open until increment 2.
- **Owned payloads.** `own int` inside an `option` or `result` payload is
  not tracked: owning-ness lives in the declaration specifiers and is erased
  from the type, so a sum type with an owned payload would need the owning
  analysis to learn about sum-type storage. Until then, a payload is an
  ordinary value and an owned object must not be stored in one.
- No `?` operator, no niche optimisation, no generic user types, no
  traits (post-1.0 per MASTER_SPEC section 32), no mutable-slice distinction
  (`mut slice<T>` is not defined yet), no pattern matching on integers or
  strings.
- No ABI stability beyond `ABI.md` section 3 and section 6: the three frozen
  rows are version 1, and changing any recorded size, alignment or field order
  is an ABI change.

## 9. Tests

| Check | What it proves |
| --- | --- |
| `make check-SAFE_TYPES` | the 16 fixtures under `gcc/testsuite/crug.dg/safe-types/` (plus the three under `bounds/` that the script drives): 32 expected passes, 0 failures |
| `scripts/check-safe-types.sh` | runs `make check-SAFE_TYPES` and tallies PASS against FAIL/XPASS/UNRESOLVED/ERROR from `gcc.sum`; prints `safe-types: 32 tests, 0 failures` |
| `scripts/check-safe-bounds.sh` | in-range indexing exits 0, an out-of-range index traps with `SIGILL`, the checked object contains at least one `ud2` and the unchecked one none, and a plain C array is never trapped |
| `gcc/testsuite/crug.dg/safe-types/layout.c` | every size, alignment and offset in `ABI.md` section 3 as `_Static_assert`s |
| `make check-C_COMPAT` | plain C and the 30-program corpus are unaffected |

Run cases are `slice-run.c`, `option-run.c`, `result-run.c`,
`bounds-off.c` and `shadowing.c`; rejection cases are `slice-non-pointer.c`,
`slice-arity.c`, `result-arity.c`, `generic-unclosed.c`,
`ctor-needs-context.c`, `ctor-wrong-type.c`, `none-payload.c`,
`match-non-exhaustive.c`, `match-not-sum.c` and `flags-off.c`.
