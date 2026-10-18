# ADR 0002 - Context-Sensitive Crug Words

- **Status:** Accepted (2026-10-18)
- **Ratified:** 2026-10-18 by CLEG0078 (project owner)
- **Date:** 2026-10-18
- **Deciders:** project owner (ratification), compiler frontend owner
- **Context doc:** MASTER_SPEC section 7, section 8, section 42; RFC-0001 sections 7.1 and 13

## Context

Crug adds words to C without reserving them: `int own = 42;` and `go = 7;` must
remain legal (MASTER_SPEC section 7), while `own Foo *p;` and `go worker(a);` must
parse as Crug. RFC-0001 section 7.1 requires that contextual words stay ordinary
identifiers "where C grammar permits", and the master spec requires collision tests
(section 42) without defining the procedure. Two implementations that choose
different lookahead produce incompatible compilers. This ADR fixes the procedure.

## Decision

### D1 - Preprocessing happens first, and wins

Crug word recognition operates on the post-preprocessing token stream. If a macro
with the same spelling is defined at the point of use, the macro wins and the Crug
reading is unavailable there. Rationale: the preprocessor is part of the C
contract; a compiler that reads behind it breaks existing code and tooling.

### D2 - The C parse is the default reading

A word is Crug only when the C reading is impossible under the token sequence
being parsed. The frontend implements this as deterministic local lookahead rather
than full backtracking, and the table below is normative. These rules are stated
for a word W in the Crug word set {own, ref, mut, raw, move, borrow, borrow_mut,
unsafe, drop, go, chan, select, scope, match, some, none, ok, err, shared}.

### D3 - Declaration-position qualifiers (own, ref, mut, raw)

At a declaration-specifier position, W is a qualifier if and only if all hold:

1. the enabling flag is on (`-fcrug-ownership` for `own`; `-fcrug-borrow` for
   `ref`/`mut`; `raw` is available when ownership is on);
2. W is not declared in the current scope as a typedef-name, object, function,
   enumerator, or macro;
3. the token after W begins a type-specifier: a C type keyword (`const`,
   `volatile`, `restrict`, `void`, `char`, `short`, `int`, `long`, `float`,
   `double`, `_Bool`, `signed`, `unsigned`, `_Complex`, `_Atomic`), `struct`,
   `union`, `enum`, a typedef-name, or `*`;
4. the resulting declaration parses.

Otherwise W is an identifier and the C parse continues.

Collision cases and their mandated readings:

| Source | Reading | Why |
|--------|---------|-----|
| `int own = 42;` | identifier | C parse valid |
| `typedef int own; own x;` | typedef | C parse valid, rule 2 fails |
| `typedef int own; own Foo *p;` (Foo a typedef) | Crug qualifier | C parse impossible (two type names) |
| `#define own __attribute__((unused)) own Foo *p;` | macro, then Crug qualifier on the expansion result | D1 |
| `struct S { own char *p; };` | field qualifier | member-declaration position, same rules |
| `int f(own char *p);` | parameter qualifier | parameter-declaration position, same rules |

For `raw`: `raw Foo *p` is a qualifier under the same rules; `raw` is never
required, and its absence means "checked".

### D4 - Statement forms

- `scope { ... }` and `unsafe { ... }`: W at statement position followed by `{`
  is the Crug construct. No valid C statement begins with an identifier followed
  by a compound statement, so no C parse exists. If W is a macro, D1 applies.
- `match (expr) { ... }`: same reasoning; `match (expr);` remains a function call.
- `chan T name;` / `chan T *name;`: W at declaration position, followed by a
  type-specifier and then an identifier, is the channel type. If `chan` is a
  typedef-name the C parse is attempted first (a C declaration of that type), and
  a subsequent identifier that cannot continue it makes the Crug reading apply.
- `go expr;`: see D5.
- `select { ... }`: statement position followed by `{`. `select` as an
  identifier in other positions is unchanged.

### D5 - `go` is ambiguous and gets one disambiguated form

`go f(x);` can be a valid C function declaration when `go` is a typedef-name and
`x` is a type name: `go f(size_t);`. The two readings cannot be separated by
lookahead alone, so:

- If the token sequence parses as a C declaration and `go` is a typedef-name,
  the declaration wins and the Crug reading requires the parenthesized form.
- The **parenthesized form `go (expr);`** is always the go-statement, because
  `go (f(a));` is not a declaration.
- When a token sequence is ambiguous and no parenthesized form is used, the
  compiler emits `warning: 'go' is ambiguous here; use 'go (call);' [-Wcrug-go-ambiguous]`.

This is the only place where Crug syntax needs a written disambiguation idiom, and
it is small enough to document in LANGUAGE.md.

### D6 - move/borrow/borrow_mut/adopt/release/drop are intrinsics, not keywords

They are ordinary identifiers that name compiler intrinsics when:

1. used in call position, and
2. not declared in the current scope by the user.

A user declaration wins silently (C compatibility). Under `-fcrug-strict`, shadowing
one of them is an error (`error: declaration of 'move' shadows a Crug intrinsic`).
This preserves the RFC's position that these are calls (RFC-0001 sections 16,
18.1 and 19) without adding reserved words.

### D7 - Feature gating

No Crug word has Crug meaning when the enabling flag is off. With flags off, the
compiler must be indistinguishable from the C frontend (permanent invariant 1).
`-fdump-crug` prints the active word set.

### D8 - Diagnostic

`-Wcrug-keyword-shadowed` (default on under `-fcrug-strict`): emitted when a word
could have been read as Crug but was read as an identifier because the C parse was
valid, and the enclosing declaration looks like a Crug construct. This is the
escape hatch that makes silent reinterpretation impossible to miss.

## Consequences

- The frontend needs no backtracking: every rule is bounded lookahead plus scope
  lookup at a declaration-specifier or statement position.
- `own`/`ref`/`mut` can appear as struct fields and parameters, which is required
  by RFC-0001 section 13 and by ADR 0004.
- Keyword tests become mechanical: one test per row of the D3 table plus one per
  D4/D5 form, in `gcc/testsuite/crug.dg/parser/keyword-collision/`.
- Cost: two named idioms in the language (`go (expr);` and the strict-mode
  shadowing prohibition). Both are documented, neither is hidden.

## Alternatives rejected

- **Reserving the words unconditionally.** Breaks C0 immediately: `int own = 1;`
  is a real pattern in C (e.g. YAML parsers) and the RFC forbids it.
- **Lexing on whitespace/position heuristics.** Unstable, untestable, and cannot
  be stated as a rule.
- **Requiring a prefix (`__crug_own`).** Loses the readability that motivates the
  project and contradicts the spec's examples.
