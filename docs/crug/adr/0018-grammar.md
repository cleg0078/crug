# ADR 0018 - Surface Grammar: productions, disambiguation and attachment

- **Status:** Proposed; normative text implemented in RFC-0001 0.2-draft sections 13-25 (RFC Appendix H)
- **Date:** 2026-10-18
- **Deciders:** project owner (ratification), compiler frontend owner
- **Context docs:** MASTER_SPEC s6.2-s6.4, s7, s8.1-s8.10, s9.1-s9.3, s13-s15, s18-s22, s25.6, s42-s43; RFC-0001 s13-s27 (L445-760); ADR 0002; ADR 0004; ADR 0005; spec/000-PRECEDENCE.md; docs/crug/EXIT-GATES.md phases 1-10

## Context

MASTER_SPEC section 8 sketches ten constructs and says the grammar "is normative in intent but may
be adjusted to fit GCC frontend structures" (L614). It never decides what a token sequence means
where two readings exist, and RFC-0001 cites the same syntax normatively (L445-461, L465-487,
L510-532, L604-611, L721-748) without a grammar. ADR 0002 fixes how a contextual word is
recognised, ADR 0004 fixes aggregate ownership and the ABI, ADR 0005 fixes destruction; none of
them fixes a production. Two implementers following the documents as written will disagree about
`own Foo **a`, `go (f(x));`, `chan int *p`, `option<option<int>>` and `close(ch)`, and no conformance
class catches the disagreement.

This ADR is the missing layer: productions, disambiguation procedures and attachment rules precise
enough to implement in `gcc/c-family` plus the C frontend's recursive-descent parser with bounded
lookahead, and precise enough to test. It is a decision record; the normative text now lives in RFC-0001 0.2-draft
sections 13, 14, 16, 18, 19, 22 and 25 (RFC Appendix H), and `docs/crug/GRAMMAR.md` remains the
phase-23 digest, derived from those sections. C0 (RFC L216-223) and the phase 1 and 3
gates require flags-off to be byte-identical to C; ABI transparency (RFC L465-487, L641-668)
requires qualifiers to change no layout, `sizeof` or calling representation. Where a decision spends
one of those, it says so.

**Numbering.** MASTER_SPEC section 79 (L4266-4283) fixes 0003-0005 as ownership metadata storage,
ownership analysis layer and drop lowering, and `docs/crug/adr/README.md` had proposed 0003 for
"ownership states". Grammar is not on the required list, so it takes the next free number, 0018.
Conflict register entry C-11 in `spec/000-PRECEDENCE.md` records the rule used: a required topic keeps
its required number, a new topic takes the next free number, and no number is reused. The decisions,
not the file name, are the artefact.

## Decision

### D1 - Feature modes, token compatibility and monotonicity

**Statement.** Crug adds no preprocessing token. The lexer is `gcc/c-family`'s unchanged:
contextual words stay `CPP_NAME`, `<` and `>` are separate punctuators, `>>` is one punctuator, and
`=>` is the two tokens `=` `>` (D8). Every Crug construct is expressible in a plain C token stream,
and no C token is reinterpreted by the lexer.

Productions are enabled per flag. The driver closes the flag set over these dependencies and never
errors because a prerequisite is missing, so EXIT-GATES phase 3 (L99) can run each flag alone:

| Flag | Productions enabled |
|------|---------------------|
| `-fcrug-ownership` | `own`, `raw`; `move`, `drop`, `adopt`, `release`; `unsafe {}` |
| `-fcrug-borrow` | `ref`, `mut`; `borrow`, `borrow_mut`; implies ownership |
| `-fcrug-goroutines` | `go`, `scope` |
| `-fcrug-channels` | `chan T`; `send`/`recv`/`close`/`channel`; `select`; implies goroutines |
| `-fcrug-slices` | `slice<T>` |
| `-fcrug-result` | `option<T>`, `result<T,E>`, `some`, `none`, `ok`, `err`; implies adt |
| `-fcrug-adt` | payload `enum`, `match` |
| `-fcrug-strict` | no production; promotes D10 warnings to errors; implies ownership + borrow |
| `-fcrug-modern` | all of the above |
| `-fcrug-kernel` | no production; removes `go`, `scope`, `chan`, `select` with an error (MASTER_SPEC 6.3 L529) |

`-fcrug-result` implies `-fcrug-adt` because `ok`/`err` are constructors of Crug sum types that MVP
consumes only through `match` (MASTER_SPEC 21 L1743). MASTER_SPEC 6.4 (L531-547) lists the other
dependencies; this ADR adds that one and changes none.

With all flags off, the token stream, the parse and the diagnostics are identical to upstream GCC:
the C parser's action tables are untouched, no contextual word has a Crug production, and every
`-Wcrug-*` option requires its flag, so none fires.

**Monotonicity.** For F1 subset of F2, every program F1 accepted is accepted by F2 with the same
meaning. Two cases fall outside, and they are the only ones: a program F1 rejected as a syntax error
may become valid (that is what a flag is for), and the parenthesised `go (call);` form with `go`
declared as a typedef-name, which ADR 0002 D5 forces to be a go-statement (D4). No other reading
changes.

**Forbids.** New tokens (`=>`, `<-`, `::`, `?`, `|T|`); reserving words; changing a token kind; any
production with its flag off; option names outside `-fcrug-`; silently ignoring an unknown
`-fcrug-*` option (EXIT-GATES L99 requires an option error); `-fownership`-style aliases in MVP
(MASTER_SPEC 6.2 L432-440 permits them, this ADR does not ship them); a `shared` qualifier or
`arc<T>` constructor (MASTER_SPEC 18 L1612-1637 says "possible"; `shared Foo *x;` is a syntax error
under every flag in MVP).

**Diagnostics and tests.** No new option. With at least one Crug flag on, a C parse error on a Crug
word carries `note: 'go' requires '-fcrug-goroutines'`; with all flags off the note is not emitted,
so flags-off diagnostics stay byte-identical. `-fdump-crug` prints the active construction set
(ADR 0002 D7). Tests in `crug.dg/flags/`: `off-tokens-1.c`, `off-parse-1.c`, `monotone-borrow-1.c`,
`closure-channels-1.c`, `closure-modern-1.c`, `kernel-no-go-1.c`; gate suites C_COMPAT (phase 1),
GNU_C_COMPAT (phase 3).

### D2 - Qualifier attachment: declaration specifiers, outermost pointer level

**Statement.** `own`, `ref`, `mut` and `raw` are declaration specifiers. They are not part of the C
type system: they qualify the declared entity and are erased wherever C compares types.

R1 - **Position.** A qualifier is well-formed only in the declaration-specifier list of a declaration
or type-name, interleaved in any order with storage-class specifiers and C qualifiers
(`static own Foo *p;` and `own static Foo *p;` are the same declaration). `Foo *own p;` is an error.

R2 - **Attachment.** The qualifier attaches to the outermost pointer level of the type being
declared, with three refinements: if the declarator declares a function it applies to the outermost
pointer level of the return type; if the declared type is an array it applies to the element type; if
the declared type has no pointer, no array and is not an owning aggregate under ADR 0004 D1, the
qualifier is an error.

| Declaration | Reading |
|-------------|---------|
| `own Foo *a;` | `a` owns one `Foo`; destructor `void (*)(Foo *)`, called with `a` |
| `own Foo **a;` | `a` owns one `Foo *` object; destructor `void (*)(Foo **)`; the inner `Foo` is not owned |
| `own Foo a[4];` | array of four owned `Foo` (ADR 0004 D7), dropped from index 3 down to 0 |
| `own Foo (*fp)(void);` | error: the outermost pointer is a function pointer, not a pointer to an object |
| `struct S { own char *name; };` | owning field; layout and `offsetof` identical to `char *name` (RFC L657-666) |
| `typedef Foo *P; own P a;` | owning pointer to `Foo`; the qualifier is at the use site |
| `typedef own Foo *P;` | error: a typedef cannot carry ownership, because the qualifier is not a type |
| `own Foo *create(void);` | the caller becomes the owner of the returned object |
| `void consume(own Foo *p);` | consuming parameter: the caller must `move`, the callee drops |
| `int f(own Foo a[4]);` | adjusted to `own Foo *a` as in C array decay; the pointer does not own elements |
| `own int i;` | error: `int` has no pointer, no array and no owned member |

R3 - **Type-names are erased, except in generic arguments.** In a type-name (`sizeof`, `alignof`,
`_Atomic(...)`, `va_arg`) a qualifier parses and is erased: `sizeof(own Foo*) == sizeof(Foo*)`
(RFC L479-483) with no diagnostic. A cast or compound literal cannot create ownership:
`(own Foo *)p` is an error, because `adopt` is the only constructor of ownership (ADR 0004 D10);
`(raw volatile uint32_t *)0x40000000` (MASTER_SPEC 8.4 L692-693) is allowed and is exactly the C cast
with checked-pointer rules suspended. Type arguments of `option`, `result`, `slice` and the element
of `chan` are *owning* type-names, where a qualifier is meaningful (D7 R5).

R4 - **Erasure at type boundaries.** For assignment compatibility, parameter lists, function-pointer
assignment, composite types and `_Generic`, qualifiers are erased: `void (*)(own Foo *)` is
compatible with `void (*)(Foo *)`, and `void f(own Foo *p)` has the type of `void f(Foo *p)`. RFC 22
(L641-655) requires the same calling representation, and an unannotated C header must keep matching a
Crug definition. Consequence: a C callback installed into a Crug-annotated function pointer consumes
without dropping; ADR 0004 D3 closes that with `.crugffi` metadata, not with types.

R5 - **Field and parameter positions.** Fields may carry a qualifier, and ADR 0004 D5/D6 then govern
drop order and forbid owned union members. Bit-fields, flexible array members and unnamed members
cannot (error).

R6 - **Combination.** At most one Crug qualifier per declaration: `own ref Foo *p;` and
`own raw Foo *p;` are errors; `raw` is the absence of checking, not an extra flag. C qualifiers compose
normally: `own const char *p` points to const char, `const != ref`, `restrict != mut` (RFC L627-633).
A qualifier inside `_Atomic(...)` is erased per R3.

R7 - **Return position.** `own` and `raw` may qualify a return type. `ref` and `mut` may not:
`error: a function cannot return a borrow`, because the lender's lifetime is not provable (EXIT-GATES
phases 6-7 reject an escaping borrow).

**Rationale.** C has one declarator grammar and no place for a prefix qualifier that distinguishes
pointer from pointee. Attaching to the outermost pointer level keeps every MASTER_SPEC 8.1 example
reading correctly, composes with C declarators without new grammar, and leaves the ABI untouched
because the qualifier is dropped before layout. `own Foo **a` deliberately owns the outer object: a
rule reaching through the second `*` would need a destructor slot per level, which the ABI lacks.

**Forbids.** `Foo *own p`; `typedef own T *P`; qualified function-pointer declarators; `own int`; two
qualifiers; a qualifier that survives a type comparison; `own` in a cast; `own` on a union member;
hidden fields, refcounts or destructor slots; a non-uniform array element own-ness.

**Diagnostics and tests.** Errors `-Wcrug-qualifier-placement` (R1, R2, R5, typedef),
`-Wcrug-qualifier-combination` (R6), `-Wcrug-cast-cannot-own` (R3), `-Wcrug-returned-borrow` (R7);
warning `-Wcrug-qualifier-redundant` for `own` on an already-owning aggregate. Tests in
`crug.dg/decl/`: `own-placement-1.c`, `own-double-pointer-1.c`, `own-array-1.c`,
`own-function-pointer-1.c`, `own-non-pointer-1.c`, `own-typedef-1.c`, `own-cast-1.c`,
`own-sizeof-1.c`, `own-param-1.c`, `own-return-1.c`, `own-field-1.c`, `own-combination-1.c`, plus
an ABI twin comparing `sizeof`/`offsetof` against plain C; gate suite OWNERSHIP (phase 4).

### D3 - Intrinsic expressions: ordinary identifiers in call position

**Statement.** `move`, `borrow`, `borrow_mut`, `adopt`, `release` and `drop` are not keywords. A name
is an intrinsic iff it is the callee of a call in primary-expression position, no declaration of that
name is visible there, and its enabling flag is on. Ordinary C lookup runs first; a macro always wins
before the parser (ADR 0002 D1).

R1 - **User declarations win.** `int move = 3;`, `void release(void *);` and `static int drop;`
compile exactly as C, and the intrinsic is unavailable under that name in that scope.
`-Wcrug-intrinsic-shadowed` reports the declaration: warning in hybrid, error under `-fcrug-strict`
(this implements ADR 0002 D6 without changing it).

R2 - **Call position only.** `&move`, `(move)(x)` and `x = move;` are errors when no user declaration
exists (`error: 'move' is a Crug intrinsic, not a function`). The intrinsic has no address.

R3 - **Operands.** Each intrinsic takes one argument unless stated; the operand must be an expression
of owned type (pointer or owning aggregate) or, for `borrow`, of owned or shared-borrowed type.

| Intrinsic | Operand | Result |
|-----------|---------|--------|
| `move(e)` | modifiable lvalue of owned type, OWNED and not borrowed, or an owned prvalue | prvalue of the same C type, OWNED; an lvalue operand becomes MOVED |
| `borrow(e)` | lvalue of owned or `ref` type whose storage outlives the borrow | `ref` value; `ref` of `ref` is a reborrow |
| `borrow_mut(e)` | modifiable lvalue of owned type with no live borrow | `mut` value |
| `adopt(e, d)` | `e` an unmanaged pointer prvalue; `d` a designator of type `void (*)(typeof(e))` | OWNED prvalue with `d` attached |
| `release(e)` | lvalue of owned type, OWNED | unmanaged prvalue; the operand becomes MOVED (MASTER_SPEC 14.4 L1168-1174) |
| `drop(e)` | lvalue of owned type, OWNED | `void`; the operand becomes DROPPED |

`move` of a prvalue is a no-op transfer, so `move(create())` and `move(move(a))` are accepted (the
latter is one transfer, not two). `move` of a compound literal of owning type is accepted: a compound
literal is an lvalue in C11 (6.5.2.5), its owned members have automatic storage, and the move
suppresses its implicit drop. `move(5)` is rejected because `int` is not owned. `drop` and `release`
of an rvalue are rejected: there is no place to leave in a moved or dropped state. `adopt` with a null
destructor is rejected: not adopting is spelled by not calling `adopt` (ADR 0004 D8).

R4 - **Conditional expressions.** `c ? move(a) : move(b)` is legal; after the join both operands are
MAYBE_MOVED and any later use or unconditional drop is rejected (RFC L506, ADR 0004 D2). The grammar
does not restrict `?:`; the ownership pass does. A conditional's ownership kind is the meet of its
arms, and arms that disagree are an error (`-Wcrug-conditional-ownership`): otherwise an owned arm's
value would become unmanaged and leak.

R5 - **Why `move(x)` must stay macro-overridable.** The spelling is a call so that a header can
`#define move(x) (x)` and compile the same source with a plain C compiler (the migration promise), so
that code which already defines these names keeps compiling, and so that tests can disable an
intrinsic. When a macro replaces `move` the operation stops transferring: the destination
initialisation becomes an implicit copy of an owned value, which the ownership pass rejects. The
override therefore cannot silently produce an unchecked program; it can only turn the program into one
the checker refuses.

**Forbids.** Keywords; intrinsic names as values; `move` of unmanaged or const values; `drop` or
`release` of rvalues; `adopt` with one argument or a null destructor; hidden runtime calls (RFC L522);
a conditional that produces ownership from mixed arms; recognising an intrinsic at a declaration site.

**Diagnostics and tests.** `-Wcrug-intrinsic-shadowed` (warn/error), `-Wcrug-intrinsic-operand`,
`-Wcrug-adopt-signature`, `-Wcrug-conditional-ownership` (errors). Tests in `crug.dg/intrinsic/`:
`move-operand-1.c`, `move-prvalue-1.c`, `move-compound-literal-1.c`, `move-conditional-1.c`,
`intrinsic-shadow-1.c`, `intrinsic-macro-override-1.c`, `intrinsic-address-1.c`,
`borrow-operand-1.c`, `adopt-signature-1.c`, `release-state-1.c`; gate suites OWNERSHIP (phase 4),
BORROW (phase 6).

### D4 - `go`: one statement, two forms, one disambiguation

**Statement.**

```ebnf
go-statement ::= "go" go-call ";"
go-call      ::= identifier "(" argument-list? ")"     (form A)
             |   "(" postfix-expression ")"            (form B; the expression must be a call)
```

Decision procedure at statement position, in order:

1. `-fcrug-goroutines` off: C only.
2. `go` is declared in scope as an object, function, typedef or enumerator: C only; `go(x);` is a
   call or the ordinary C error.
3. The next token is `:`: C label (`go: while (1) ...`).
4. The next token is an identifier followed by `(`: attempt the C declaration parse (ADR 0002 D2). If
   it is a valid declaration and `go` is a typedef-name, the declaration wins and form A is
   unavailable; otherwise form A applies.
5. The next token is `(`: form B; the go-statement wins, with `-Wcrug-go-ambiguous` if the C
   declaration parse also succeeds.
6. Anything else: C only (which rejects `go + 1;` and `go;`).

Form A's identifier must name a function returning `void`; a call through a function pointer is form B,
`go ((*fp)(a));`. Arguments follow C rules plus capture rules: an owned operand is passed with `move`
(ADR 0004 D1), and a borrowed capture requires the enclosing `scope` (ADR 0005 D7).

**Rationale.** MASTER_SPEC 8.5 (L704-716) writes `"go" postfix-expression "(" args ")" ";"`; this ADR
narrows that to forms A and B, because the postfix-expression reading cannot be separated from a C
declaration without unbounded lookahead and because the master spec itself says "Do not initially
support arbitrary expressions after `go`" (L716). ADR 0002 D5 says the parenthesised form "is always
the go-statement, because `go (f(a));` is not a declaration"; that fails when every argument is a
type-name (`typedef int go; go (f(size_t));` declares `f`). This ADR keeps ADR 0002's reading
(go-statement wins) and adds the warning there; that corner is the monotonicity exception D1 records.

**Forbids.** `go` in expression position (`x = go f();`); `go a + b;`; a non-`void` callee; a callee
that is an object; detached tasks (every child belongs to a `scope`, ADR 0005 D7); omitting the `;`;
`go` as a keyword.

**Diagnostics and tests.** `-Wcrug-go-ambiguous` (warn/error, ADR 0002 D5); `error: 'go' callee must
return void`; `error: 'go' requires a function`. Tests in `crug.dg/goroutines/`: `go-ambiguous-1.c`,
`go-paren-1.c`, `go-typedef-1.c`, `go-label-1.c`, `go-return-value-1.c`, `go-expression-1.c`,
`go-capture-1.c`; gate suite GOROUTINES (phase 14).

### D5 - `scope` and `unsafe` compound statements

**Statement.** Both are statements and require a compound statement:

```ebnf
scope-statement  ::= "scope"  compound-statement
unsafe-statement ::= "unsafe" compound-statement
```

R1 - **Statement position only.** `scope x;` and `x = unsafe { ... };` are errors. Both may be
labelled (`retry: scope { ... }`), may follow `case`/`default` labels, and may appear anywhere a
statement may. Elsewhere they stay ordinary identifiers (`int scope;`, `scope(3);`).

R2 - **The block is a C block.** Declarations may appear anywhere inside it (C99 rules), scoping and
shadowing are C's, storage is automatic, and a declaration may be the first token. Drops run at block
exit in reverse declaration order (ADR 0005 D3).

R3 - **`scope` has one entry and one completion.** Every exit - reaching `}`, `goto` out,
`break`/`continue` crossing the boundary, or `return` - is a completion point: the generated code waits
for every child spawned in the scope, then runs the block's drops (ADR 0005 D7; MASTER_SPEC 8.6
L734). Children of a nested scope belong to the nested scope. A child that could outlive its scope is
rejected (EXIT-GATES phase 15).

R4 - **Jumps.** `goto` into a `scope` block is an error for the reason in ADR 0005 D3 (no children
state and no drop state on entry). `goto` out is legal and gets the wait and drops duplicated on the
edge. `return` inside a scope waits and drops on the return edge.

R5 - **`unsafe` is a marker.** It creates a C block, has no codegen (RFC L621, MASTER_SPEC 15.3
L1253-1255), may be nested, and suppresses exactly the strict-mode checks of MASTER_SPEC 15.2 (raw
dereference, integer/pointer casts, forced aliasing, inline asm touching owned state, unknown FFI,
`longjmp` across owned cleanup). It does not suspend O1 or B1: double move, use-after-move and borrow
conflicts are still errors inside `unsafe`.

**Rationale.** A compound statement needs no new brace grammar and reuses C's block scoping, drop order
and label rules. The wait-on-every-exit rule is what makes `scope` structured: an exit that skipped the
wait would let a child outlive its parent, which is the one property phase 15 tests.

**Forbids.** Expression forms; declarations without braces (`scope Foo *p;`); `goto` into; a child
outliving the scope; `unsafe` disabling ownership or borrow checking; `unsafe` changing codegen; a
single-statement body without braces.

**Diagnostics and tests.** `error: expected '{' after 'scope'`; `-Wcrug-jump-into-scope` (error, same
family as ADR 0005 D3); `error: scope statement cannot appear in an expression`. Tests:
`crug.dg/scope/` - `scope-label-1.c`, `scope-goto-out-1.c`, `scope-goto-in-1.c`,
`scope-return-waits-1.c`, `scope-decl-first-1.c`, `scope-nested-1.c`; `crug.dg/strict/` -
`unsafe-block-1.c`, `unsafe-no-codegen-1.c`, `unsafe-double-move-1.c`; gate suites SCOPE (phase 15),
STRICT_UNSAFE (phase 8).

### D6 - `chan` declarations, channel operations and `select`

**Statement: the type.**

```ebnf
channel-type ::= "chan" type-name
```

The type-name is the longest prefix that can be an abstract declarator and does not consume an
identifier; the tokens after it are the declarator. Two deliberate consequences: `chan int *jobs;`
declares a channel whose element type is `int *` (the type-name is `int *`), and
`chan own Job *jobs;` (MASTER_SPEC 8.7 L749) is a channel of owning pointers. A pointer to a channel has
no declaration spelling; use `typedef chan int CI; CI *p;`. `chan int a[4];` is an array of four
channels.

**Statement: the declaration ambiguity.** ADR 0002 D4 governs and this ADR adds nothing: the C parse
is attempted first and wins when it succeeds.

| Source | Reading |
|--------|---------|
| `chan(x);` | call to a function named `chan` (`chan` followed by `(` is not a type-name start) |
| `typedef int chan; chan x;` | C declaration of `x` with type `chan` |
| `typedef int chan; chan int jobs;` | channel declaration: the C parse fails at `int` (two type names), so ADR 0002 D2 applies |
| `int chan; chan int jobs;` | channel declaration, with `-Wcrug-keyword-shadowed` naming the shadowed object |
| `int chan; chan = jobs;` | C assignment |
| `typedef int chan; chan chan;` | C declaration of a variable `chan` of type `chan` |

**Statement: operations.** `send(ch, v)`, `recv(ch)`, `close(ch)`, `channel(n)` are call syntax, not
operators; there is no `<-` operator in Crug. `send` and `close` return `void`, `recv` returns the
element type, and `channel(n)` is type-directed: valid only where a channel type is known from context
(initialiser, assignment, argument, return, cast), otherwise `-Wcrug-channel-shape`.

R1 - **Type-directed selection.** With `-fcrug-channels` on, a call named `send`, `recv` or `close` is
the channel operation iff the arity matches (2, 1, 1), the first argument has channel type, and no
declaration of that name in scope accepts the argument types. Otherwise it is an ordinary C call. So
`close(fd)` is libc's, `close(ch)` is Crug's, `send(sock, buf, len)` is POSIX's and `send(ch, v)` is
Crug's. A user `void close(chan int)` wins over the builtin and is reported by
`-Wcrug-intrinsic-shadowed`.

R2 - **Owned elements move.** If the element type is owned, `send(ch, v)` requires `move(v)`: the
send transfers ownership and the sender must not use or drop `v`. `recv(ch)` yields an OWNED prvalue.
`chan int` elements are copied, never moved.

R3 - **`select`.** `case` and `default` inside `select { }` are select labels, not C labels; the
suspension is scoped to the select body, so a `switch` in an arm body uses C labels again.

```ebnf
select-statement ::= "select" "{" select-arm* "}"
select-arm       ::= "case" select-comm ":" statement-list
                 |   "default" ":" statement-list
select-comm      ::= [ identifier "=" ] "recv" "(" expression ")"
                 |   "send" "(" expression "," expression ")"
```

```c
select {
case value = recv(input): process(value);
case send(output, value):  sent++;
default:                   yield();
}
```

R4 - **Arm rules.** No fallthrough: an arm ends at the next `case`, `default` or `}`, and `break` exits
the select. Exactly one `default`, and it must be last (deterministic lowering; MASTER_SPEC 8.8
L766-777 does not order it). An empty `select {}` blocks forever and is documented.
`case v = recv(ch):` binds `v` as an lvalue scoped to the arm body; an owned element is OWNED there and
dropped at arm exit unless moved. `case recv(ch):` discards the value and drops it at arm exit if
owned. `case v == recv(ch):` is an error with `note: use 'case v = recv(ch):'`.

R5 - **No `select` in expressions.** Like `go` and `match`, `select` is statement-only in MVP; a
value-returning select needs the `option` ABI question answered first (open question 4).

**Rationale.** Greedy element types follow MASTER_SPEC 8.7's own example, which only reads correctly if
`own Job *` is the element. Type-directed operation selection is the only rule that lets `close` stay
libc's `close` in a file that also closes channels; a rename would break POSIX code, and a scope rule
would make the meaning of `close(ch)` depend on whether `<unistd.h>` was included.

**Forbids.** `<-`; channel operators; a pointer declarator on `chan` without a typedef; `select` as an
expression; fallthrough; two `default`s or a non-last `default`; a `case` that is not a receive or
send; `send` of an owned lvalue without `move`; `channel(n)` without a type context.

**Diagnostics and tests.** `-Wcrug-channel-shape` (error), `-Wcrug-select-form` (error),
`-Wcrug-intrinsic-shadowed` (warn/error). Tests in `crug.dg/channels/`: `chan-call-1.c`,
`chan-typedef-1.c`, `chan-object-1.c`, `chan-element-type-1.c`, `chan-array-1.c`,
`send-owned-1.c`, `close-libc-1.c`, `recv-binding-1.c`, `channel-context-1.c`, `select-arm-1.c`,
`select-default-1.c`, `select-nested-switch-1.c`, `select-empty-1.c`; gate suites CHANNELS and
SELECT (phase 16).

### D7 - `option<T>`, `result<T,E>`, `slice<T>`: type position only

**Statement.** `<` and `>` are never reinterpreted in expression position. `a < b` is less-than, `a >> b`
is a right shift, and no expression-position token changes meaning when a flag is turned on. Generic
types exist only in type position.

R1 - **Type positions, exhaustively.** (i) declaration specifiers at the start of a declaration,
including struct and union members; (ii) parameter types; (iii) type-names: after `(` when the sequence
is a cast, and the operand of `sizeof`, `alignof` or `offsetof`; (iv) type arguments inside `<...>`;
(v) the `chan` element; (vi) `typedef`.

R2 - **Recognition rule.** `NAME <` opens a type-argument list iff `NAME` is `option`, `result` or
`slice`, its flag is on, and the token sequence up to the matching `>` parses as a type-argument list
followed by a token that can begin or continue a declarator (`*`, identifier, `(`, `[`, `,`, `)`, `;`,
`=`). Otherwise the construct is not a generic type and C rules decide. The scan is bounded by angle and
parenthesis depth; no whole-declaration backtracking is needed.

R3 - **`>>` and whitespace.** A `>>` token closes two argument lists when the angle depth is at least 2
(`result<option<int>, Error>`, `option<result<option<int>, Error>>`); the parser splits it only in type
position, as C++11 does. In expression position it stays a shift. Whitespace and comments are
insignificant between the constructor and `<` and between arguments: at declaration position
`option < int > x;` is `option<int> x;`.

R4 - **Shadowing.** If `option`, `result` or `slice` is declared in scope as an object or function, the C
parse wins where it succeeds: `option < T > x;` is a relational expression statement, reported by
`-Wcrug-keyword-shadowed`. If it is a typedef-name, `option<int> x;` is neither a C expression nor a C
declaration, so the generic reading applies and the typedef is shadowed (same option). This is what
keeps enabling a flag from changing a valid C program (D1).

R5 - **Ownership in arguments and the constructors.** Type arguments are *owning* type-names (D2 R3),
so `result<own Foo *, Error>` (MASTER_SPEC 8.9 L788) has an owned payload: the ok payload is transferred
by move or return, dropped with the value, and only the active variant's owned fields are dropped
(RFC L743-748, MASTER_SPEC 22 L1759-1763). `option<own Foo *>` likewise. `some(v)`, `none`, `ok(v)` and
`err(e)` are type-directed expression constructors like `channel(n)`, and a declaration of one of those
names wins over the constructor (ADR 0002 D2). No niche optimisation in MVP:
`sizeof(option<own Foo *>) != sizeof(Foo *)` is permitted, because MASTER_SPEC 20 L1711-1718 defers
niche representation until ABI rules are formalised. Layout goes in `docs/crug/ABI.md` before phase 10
closes (EXIT-GATES L181-184).

R6 - **`slice<T>`** is a borrowed view `{ T *ptr; size_t len }`; the slice never owns its elements, and
`slice<own Foo *>` is a view of a sequence of owning pointers. Bounds checks apply to slice indexing
only, never to plain C pointers (RFC L725-729).

R7 - **No general generics.** `vector<int>` where `vector` is a typedef is an error
(`-Wcrug-generic-form`): only builtin constructors parse in MVP (MASTER_SPEC 23 L1769-1781).
`option x;` without arguments is the same error. The `?` propagation operator of MASTER_SPEC 21
L1737-1743 is not syntax in MVP (EXIT-GATES phase 10 non-goal): `x?` is a C syntax error.

**Rationale.** A type-directed rule that only fires in type position is the only way to add
generic-looking types without a new lexer and without changing any C expression. Requiring a
declarator-continuation token after `>` keeps `(option < int) > x` and `option < T > x` in the C
reading when the constructor name is an ordinary identifier.

**Forbids.** General templates; user-defined generic constructors; `<` disambiguation in expressions;
splitting `>>` outside type position; `option` or `result` without arguments; niche representation;
`?`; an unqualified `option` used as a variable without a diagnostic.

**Diagnostics and tests.** `-Wcrug-generic-form` (error), `-Wcrug-keyword-shadowed` (warn/error). Tests
in `crug.dg/generic/`: `option-decl-1.c`, `result-owned-1.c`, `nested-close-1.c` (`>>` and `>>>`),
`sizeof-generic-1.c`, `cast-expression-1.c` (`(a < b)`), `less-than-shadow-1.c`,
`slice-element-1.c`, `unknown-generic-1.c`; gate suite SAFE_TYPES (phase 10).

### D8 - Payload enums and `match`: statement only, `=>` arms, `_` catch-all

**Statement: the enum declaration.** With `-fcrug-adt`, an `enum` declaration is a payload enum if
and only if at least one enumerator carries a parenthesised payload, and a payload is exactly one
type-name:

```ebnf
payload-enum    ::= "enum" identifier? "{" payload-variant ("," payload-variant)* ","? "}"
payload-variant ::= identifier [ "(" type-name ")" ]
```

`enum Message { Text(own char *), Image(own Image *), Quit, };` is a payload enum with two payload
variants and one unit variant. `enum Color { RED, GREEN };` stays an ordinary C enum even with the
flag on, because no enumerator has a payload, so enabling `-fcrug-adt` never changes an existing C
program. Empty parentheses are not a payload: `enum E { A(), B() };` is an error
(`-Wcrug-adt-form`), because a payload enum with no payload is indistinguishable from a C enum and
would make "add a payload later" an incompatible change. Construction is `Text(s)` for a payload
variant and the bare enumerator `Quit` for a unit variant; the variant names are ordinary
identifiers, so a user function with the same name is a C redefinition. A payload type may be an
owning type (`own char *`); moving it into the constructor is ADR 0004's aggregate rule, not a new
grammar rule.

**Statement: `match`.**

```ebnf
match-statement ::= "match" "(" expression ")" "{" match-arm* "}"
match-arm       ::= match-pattern "=>" match-body ","?
match-body      ::= expression | compound-statement
match-pattern   ::= variant "(" identifier ")" | variant | "_"
```

`variant` is `ok`, `err`, `some`, `none`, or a C enumerator of a payload enum; patterns are unqualified
in MVP. `=>` is the two tokens `=` `>` (no new token), accepted only in arm position, with optional
whitespace or comments between them; everywhere else `=` and `>` keep their C meaning.

R1 - **Statement only.** `int x = match (r) { ... };` is an error with `note: assign inside the arms`.
MVP has no match expression, because an expression form needs a result type, which needs the
`option`/`result` ABI (D7 R5).

R2 - **Arms.** An expression body must be followed by `,` unless the next token is `}`; a block body is
self-delimiting and may be followed by an optional `,`. A trailing comma after the last arm is always
accepted (MASTER_SPEC 8.10 L796-800 shows it). An expression body's value is discarded, so an arm may
call a `void` function or assign; `-Wunused-value` still applies to a pointless one.

R3 - **Exhaustiveness.** Every variant of the scrutinee's type must be covered or a `_` arm must be
present, and `_` must be last. A missing variant is an error naming the missing variant (EXIT-GATES L184
expects exit 1 without `-fcrug-strict`). `default` is not a match arm
(`error: use '_' for the catch-all in 'match'`), because a colon form would reintroduce C's label syntax
next to `select`. Duplicate or unreachable arms are `-Wcrug-match-unreachable` (warn; error in strict).

R4 - **Bindings.** A binding introduces a scope consisting of the arm body; it shadows outer names, is
invisible in later arms and after the match, and is an lvalue of the payload type. An owned payload
binding is OWNED and is dropped at arm exit unless moved. MVP patterns bind at most one payload; `_`
binds nothing and is the wildcard in every binding position.

R5 - **Owned scrutinee.** If the scrutinee type has owned payloads, the scrutinee must be
`move(lvalue)`; binding a payload then transfers it out of the moved value, and the remaining value is
never dropped because it was moved. A plain lvalue scrutinee with owned payloads would be a partial
move, which ADR 0004 D4 rejects.

**Rationale.** Arms separated by `=>` and `,` cannot be confused with C labels, which is why `match` does
not reuse `case` and `:`; `select` keeps the colon form because MASTER_SPEC 8.8 shows it. Statement-only
keeps exhaustiveness local to a statement and avoids committing to a value representation before
`ABI.md` exists.

**Forbids.** Match as an expression; `default:`; non-exhaustive match; `_` not last; multi-payload
patterns; qualified variants; bindings escaping the arm; `=>` outside an arm; fallthrough; matching a
non-sum type (`error: 'match' requires 'option', 'result' or a payload enum`).

**Diagnostics and tests.** `-Wcrug-match-form` (error), `-Wcrug-match-non-exhaustive` (error),
`-Wcrug-match-unreachable` (warn/error). Tests in `crug.dg/match/`: `arm-expression-1.c`,
`arm-block-1.c`, `trailing-comma-1.c`, `wildcard-1.c`, `non-exhaustive-1.c`, `default-rejected-1.c`,
`binding-scope-1.c`, `binding-owned-1.c`, `match-expression-1.c`, `nested-match-1.c`; gate suites
SAFE_TYPES (phase 10), ADT (phase 11).

### D9 - Preprocessor and macro interaction

R1 - **The parser sees post-preprocessing tokens.** There is no lexer rule and no origin check: whatever
tokens survive `cpp` are what the parser recognises, and a macro definition at the use site wins over
every Crug reading (ADR 0002 D1).

R2 - **A macro that replaces a word removes its Crug reading.** `#define own my_own` makes
`own Foo *p;` parse as `my_own Foo *p;`; the qualifier is unavailable there and the code is unmanaged.
`-Wcrug-word-macro` reports the definition once, at the definition site, naming the flag whose construct
is disabled; it is a warning in every mode, never an error, because mapping a Crug word to a
compatibility shim is a legitimate way to build one source with two compilers.

R3 - **A macro that produces a word enables it.** `#define OWNABLE(T) own T *` followed by
`OWNABLE(Foo) p;` yields a qualifier at the expansion site, and `#define M own` used as `M Foo *p;`
does too. Diagnostics carry GCC's ordinary macro-expansion chain
(`note: in expansion of macro 'OWNABLE'`); the parse is identical to the spelling without the macro.

R4 - **Stringification and pasting are protected by C, not by us.** `#x` where `x` expands to `own`
produces the string `"own"` and the parser never sees it. `own ## _t` produces the single identifier
`own_t` and is never a qualifier. `_Pragma` is unaffected. A macro parameter named `own` is a
parameter, not a qualifier, except where the argument itself is the token `own` (MASTER_SPEC 42
L2810-2815 lists these cases; they need no new rule).

R5 - **No Crug construct hides behind a macro silently.** Because R2 and R3 are the only two
directions, a token sequence either reaches the parser as the word or does not; there is no third state.
`-Wcrug-word-macro` and `-Wcrug-keyword-shadowed` cover the two surprises.

R6 - **Feature macros.** The driver predefines `__CRUG__` (always, value 1) and one macro per flag
(`__CRUG_OWNERSHIP__`, `__CRUG_BORROW__`, `__CRUG_GOROUTINES__`, `__CRUG_CHANNELS__`,
`__CRUG_SLICES__`, `__CRUG_RESULT__`, `__CRUG_ADT__`, `__CRUG_STRICT__`) with value 1 when the flag is
on. They are additions to the predefined set, not changes to the token stream of a program that does not
test them; version macros belong to ADR 0013.

**Forbids.** Lexer-level keyword recognition; parsing that depends on whether a token came from a macro;
a `#define` of a Crug word that silently changes a Crug construct's meaning without
`-Wcrug-word-macro`; treating a stringified Crug construct as source; a Crug construct in a macro body
that would not parse when written out literally.

**Diagnostics and tests.** `-Wcrug-word-macro` (warn/warn). Tests in `crug.dg/pp/`:
`macro-defines-own-1.c`, `macro-expands-to-own-1.c`, `macro-param-own-1.c`, `stringify-own-1.c`,
`paste-own-1.c`, `feature-macros-1.c`; gate suites GNU_C_COMPAT (phase 3), C_COMPAT (phase 1).

### D10 - Diagnostics and test obligations

**Statement.** Every new warning has a `-Wcrug-*` option; Crug-only hard errors carry one where a test
must name them, and every diagnostic has a stable code `crug-<AREA>-<NNN>` in the registry
(`docs/crug/DIAGNOSTICS.md`) that
EXIT-GATES phase 4 (L113) and phase 20 require. `-fcrug-strict` promotes exactly the options marked
"error" in the strict column and nothing else (conflict C-08, spec/000-PRECEDENCE.md L44).

| Option | Hybrid | Strict | Decision |
|--------|--------|--------|----------|
| `-Wcrug-keyword-shadowed` | warn | error | D1, ADR 0002 D8 |
| `-Wcrug-intrinsic-shadowed` | warn | error | D3 R1, D6 R1 |
| `-Wcrug-go-ambiguous` | warn | error | D4, ADR 0002 D5 |
| `-Wcrug-qualifier-placement` | error | error | D2 R1, R2, R5, typedef |
| `-Wcrug-qualifier-combination` | error | error | D2 R6 |
| `-Wcrug-qualifier-redundant` | warn | error | D2 R2 |
| `-Wcrug-cast-cannot-own` | error | error | D2 R3 |
| `-Wcrug-returned-borrow` | error | error | D2 R7 |
| `-Wcrug-intrinsic-operand` | error | error | D3 R2, R3 |
| `-Wcrug-adopt-signature` | error | error | D3 R3 |
| `-Wcrug-conditional-ownership` | error | error | D3 R4 |
| `-Wcrug-jump-into-scope` | error | error | D5 R4, ADR 0005 D3 |
| `-Wcrug-channel-shape` | error | error | D6 R1, R2 |
| `-Wcrug-select-form` | error | error | D6 R3, R4 |
| `-Wcrug-generic-form` | error | error | D7 R7 |
| `-Wcrug-adt-form` | error | error | D8 (payload enum and unit-only rejection) |
| `-Wcrug-match-form` | error | error | D8 R1, R3, R5 |
| `-Wcrug-match-non-exhaustive` | error | error | D8 R3 |
| `-Wcrug-match-unreachable` | warn | error | D8 R3 |
| `-Wcrug-word-macro` | warn | warn | D9 R2 |
| `-Wcrug-unadopted-own` | warn | error | ADR 0004 D8 |
| `-Wcrug-ffi-ownership` | warn | error | ADR 0004 D3 |
| `-Wcrug-owned-byte-copy` | warn | error | ADR 0004 D9 |
| `-Wcrug-setjmp-drops` | warn | error | ADR 0005 D3 |
| `-Wcrug-drop-unreachable` | warn | error | ADR 0005 D8 |

Two rules constrain how they are used: no diagnostic fires with all flags off (D1), and no diagnostic
quotes GIMPLE, IR or sanitizer output (EXIT-GATES L301) - fix instructions name the source construct and
the flag. Every message quotes the source location, the construct name and, for a rejected operand, the
operand's type (MASTER_SPEC 37 L2611-2617).

| Decision | Directory | Gate suite |
|----------|-----------|------------|
| D1 | `crug.dg/flags/` | C_COMPAT, GNU_C_COMPAT (phases 1, 3) |
| D2 | `crug.dg/decl/` | OWNERSHIP (phase 4) |
| D3 | `crug.dg/intrinsic/` | OWNERSHIP, BORROW (phases 4, 6) |
| D4 | `crug.dg/goroutines/` | GOROUTINES (phase 14) |
| D5 | `crug.dg/scope/`, `crug.dg/strict/` | SCOPE, STRICT_UNSAFE (phases 15, 8) |
| D6 | `crug.dg/channels/` | CHANNELS, SELECT (phase 16) |
| D7 | `crug.dg/generic/` | SAFE_TYPES (phase 10) |
| D8 | `crug.dg/match/`, `crug.dg/adt/` | SAFE_TYPES, ADT (phases 10, 11) |
| D9 | `crug.dg/pp/` | GNU_C_COMPAT (phase 3) |
| D10 | `crug.dg/diagnostics/` | DIAGNOSTICS (phase 20) |

The collision corpus of ADR 0002 and MASTER_SPEC 42 stays in `crug.dg/parser/keyword-collision/`; this
ADR adds no test there and changes none.

## Consequences

- The parser stays recursive-descent with bounded lookahead everywhere: D1 (flag tables), D2
  (attachment is a property of the declarator already built), D3 (scope lookup before intrinsic), D4
  (one statement-position test), D6 (C parse first, then the type-directed call), D7 (a scan to the
  matching `>` plus a continuation-token test), D8 (arm position only). No construct requires
  backtracking over a declaration.
- The C frontend needs three hooks that do not exist today: a declaration-specifier hook for qualifiers,
  a statement-position hook for `go`/`scope`/`unsafe`/`select`/`match`, and a type-name hook for the
  four constructors. D7's rule belongs in the type-name entry of `gcc/c-family/c-parse.cc`, not in the
  lexer.
- Qualifiers are erased early, so the phase 4 ABI tests can compare `sizeof`, `offsetof` and object code
  against plain C twins and expect zero differences.
- `-fcrug-strict` now has a closed meaning: it promotes the D10 warnings plus the options ADR 0004 and
  ADR 0005 already define, and adds no grammar.
- Several legal C constructs become errors under a flag (`own` on a function pointer, `option` without
  arguments, a user `close` taking a channel). Each is a decision with a named diagnostic, not an
  accident.
- Cost: three named idioms (`go (call);`, a typedef for pointer-to-channel, `_` instead of `default` in
  match) and one reinterpreted corner (`go (f(size_t));`), all documented and tested in the directory of
  the feature they belong to.
- This ADR does not decide borrow rules (ADR 0006), the diagnostic registry (ADR 0012), the ABI of
  Crug-native types (`docs/crug/ABI.md`), or the runtime.

## Rejected alternatives

- **Reserving the words globally.** Breaks C0 on contact: `int own = 1;` and `int go = 2;` are real C
  (MASTER_SPEC 7.1 L581-586, RFC L454-461), and the kernel class is defined on GNU C. ADR 0002 already
  rejects this.
- **Making `move`/`borrow`/`drop` real keywords.** Same breakage with no gain: a keyword buys nothing
  that call-position lookup does not, and it removes the macro override D3 R5 depends on.
- **A new declarator spelling, `Foo *own a`.** It reads better for `own Foo **a` but needs a second
  qualifier position in the C declarator grammar, breaks every MASTER_SPEC 8.1 example, and cannot
  qualify an array-declared parameter.
- **C++-style templates for user types (`vector<int>`).** A general `Name<Args>` rule would have to
  disambiguate `a < b > c` in expression position, which D7 refuses; user generics are post-MVP
  (MASTER_SPEC 23).
- **Keyword-less generics through attributes (`__attribute__((crug_option(T)))`).** Attributes are
  ignorable by other compilers, so the same source would compile as plain C with a silently different
  type; it also hides the syntax from readers and from tagging tooling.
- **A `go` sigil or a reserved `go` (`go$`, `@go`).** A new token and new line noise, for a
  disambiguation D4 resolves with one statement-position test.
- **A Go-style `<-` receive operator.** A new punctuator for one operation, and `recv(ch)` is already in
  MASTER_SPEC 8.7; the operator would also make `a < -b` a lexing question.
- **Renaming `close`/`send`/`recv` to avoid libc.** Every POSIX program would need a compatibility macro,
  and the channel operation would still be ambiguous for whoever writes one; type-directed selection
  (D6 R1) needs no rename.
- **`=>` as a new preprocessing token.** It would change the token stream for every C program containing
  `= >`, however rare; the two-token reading costs one parser test.
- **Match as an expression, or `default:` in match.** Both need a value representation and a colon label
  this ADR cannot promise before `ABI.md`; the statement form is enough for phase 10.
- **Hidden moved bits or runtime drop flags, to make `move` and `match` easier.** Forbidden by ADR 0004
  and RFC L487; not reconsidered here.

## Test obligations

Every decision lands in a directory under `gcc/testsuite/crug.dg/` (mapping in D10), with the naming
convention `<construct>-<case>-<n>.c` (`n` increments per case from 1) and one `dg-options` line naming
the exact flags. Negative tests use `dg-error`/`dg-warning` with the option name in the message suffix;
positive tests compile, or run and assert output. The minimum set per decision:

- `crug.dg/flags/`: token-stream identity against plain C (`off-tokens-1.c`), parse identity
  (`off-parse-1.c`), one monotonicity file per subset pair, and the closure tests
  (`closure-channels-1.c`, `closure-modern-1.c`).
- `crug.dg/decl/`: one file per row of D2's reading table, plus the ABI twin.
- `crug.dg/intrinsic/`: one file per row of D3 R3's operand table, the shadowing case, the macro
  override, and the conditional join.
- `crug.dg/goroutines/`: forms A and B, the typedef case, the label case, the non-void rejection, and an
  owned capture that must be moved.
- `crug.dg/scope/` and `crug.dg/strict/`: labels, goto out, goto in rejected, return waits, declarations
  first, nested scopes; unsafe with and without strict, and an `unsafe` double move that must still fail.
- `crug.dg/channels/`: all six declaration-ambiguity rows, the element-type rule, an array of channels,
  an owned send counted once across producer and consumer, `close(fd)` next to `close(ch)`, both select
  comm forms, `default` last, and a nested switch inside an arm.
- `crug.dg/generic/`: declarations, owned type arguments, nested closing angles, `>>` in expression
  position unchanged, a cast, a `sizeof`, and a shadowed constructor.
- `crug.dg/match/`: both arm bodies, trailing comma, wildcard last, non-exhaustive rejected,
  `default:` rejected, binding scope and owned binding, match in expression position rejected.
- `crug.dg/adt/`: a payload enum declaration, a unit variant, a C enum staying a C enum with the flag
  on, empty parentheses rejected, construction of payload and unit variants, an owned payload moved into
  the constructor.
- `crug.dg/pp/`: the four macro directions of D9 plus the feature macro set.

Each file also runs in its gate suite (`make check-<SUITE>` from EXIT-GATES phases 4-16), so a decision
without a green test blocks the phase that needs it. Tests are added in the phase that implements the
construct, not all at once.

## RFC-0001 amendments (implemented in 0.2-draft)

This ADR is a decision record; it adds no MUST. RFC-0001 carries obligation only in uppercase (section 1),
and nothing above is uppercase-normative. RFC-0001 0.2-draft implements the seven amendments below
(RFC Appendix H). The list is kept so the reasoning stays traceable; the RFC text governs where it
differs from this ADR. The RFC was revised from 0.1-draft, so its line numbers changed: every reference
below and elsewhere in this documentation set names the section, not a line.

1. Section 13: replaced the bare word list with the classification, positions and productions of
   13.1-13.4, states that Crug adds no preprocessing token, and cites this ADR as rationale;
   `docs/crug/GRAMMAR.md` stays the phase-23 digest.
2. Section 14: added the attachment rule (D2 R2) and the erasure rule at type boundaries (D2 R4); the
   `sizeof` equalities stay correct and become derivable rather than stipulated.
3. Section 16: states that `move`, `borrow`, `borrow_mut`, `adopt`, `release` and `drop` are
   intrinsic identifiers in call position, not keywords (D3), so "MUST NOT call a hidden move runtime
   function" keeps its meaning.
4. Section 18: added the operand rules for `borrow`/`borrow_mut` (D3 R3); B1 otherwise cites
   syntax only.
5. Section 19: fixed the spellings of `adopt` (two arguments), `release` and `drop` (D3); the
   non-claim about abnormal control flow is unaffected.
6. Section 22: adds that qualifiers are erased for type compatibility and names `option`,
   `result`, `slice`, `chan` and payload enums as the Crug-native types whose ABI needs documentation.
7. Section 25: replaced the "conceptually" descriptions with the productions of D6 and D7; the ABI and
   `?`-propagation sentence ("require a focused RFC") stays, so `option`/`result` syntax is fixed here
   but their layout is not.

## Open questions

1. Should `own` on a non-pointer owning aggregate (`own struct S s;`) stay legal and redundant, or become
   an error? **Recommended:** legal, with `-Wcrug-qualifier-redundant`, because c2crug will want to mark
   ownership explicitly at the declaration that creates it.
2. Should `-fcrug-modern` include `-fcrug-strict`? **Recommended:** yes, as D1 decides: MASTER_SPEC 6.3
   lists "strict diagnostics" in the bundle, and any other reading makes `-fcrug-modern` and
   `-fcrug-modern -fcrug-strict` differ for no reason a user could learn.
3. For `chan T name;`, should the element type be the greedy type-name (channel of `int *`) or the base
   specifier (pointer to channel)? **Recommended:** greedy, because MASTER_SPEC 8.7's
   `chan own Job *jobs` only reads correctly that way, and the typedef escape covers pointer-to-channel;
   if the owner disagrees, a parenthesised element type would be a new production, not a reinterpretation.
4. What should `recv` do on a closed channel - return `option<T>`, or take an out-parameter
   `recv(ch, &v)`? **Recommended:** return `option<T>` when `-fcrug-result` is on and plain `T`
   otherwise, with no out-parameter form; the out-parameter would be the only Crug operation that requires
   a pointer to a declared local.
5. Should a `match` on owned payloads require `move(lvalue)` (D8 R5), or should the scrutinee be
   implicitly moved? **Recommended:** require the explicit move, because the implicit form makes
   `match (r)` mean "inspect" for scalar payloads and "consume" for owned ones, which is exactly the
   hidden difference ADR 0004 D1 exists to prevent.
6. Should whitespace be insignificant between `=` and `>` in `=>`, and between a constructor and `<`?
   **Recommended:** yes to both (D7 R3, D8 R1): in type and arm position the split tokens have no
   competing reading, and adjacency rules break on comments and line splices.
7. Should `typedef own Foo *P;` be an error (D2 R2) or legal? **Recommended:** keep it an error, because
   ownership is not part of the type and a typedef that carries it would make `P` unusable wherever a
   plain `Foo *` is expected; `typedef Foo *P;` plus `own P a;` at the use site covers the convenience
   case with no hidden state.
8. Should `enum E { A(), B() };` stay an error (D8), or become sugar for unit variants? **Recommended:**
   keep the error until a use case appears, so that adding a payload to a variant later is never an
   incompatible change.
