# The grammar of Crug

Status: **Phase 23 implemented** (2026-10-21). The surface grammar the
compiler implements: lexical rules, declarations and qualifiers, statements,
and the checked type forms. The normative productions are in
[adr/0018-grammar.md](adr/0018-grammar.md) and `spec/RFC-0001` sections 13-25.

A Crug source file is an ordinary C translation unit until a feature option is
given, and even then every new construct is recognized only where it cannot be
valid C. [LANGUAGE.md](LANGUAGE.md) section 3 lists the options.

## 1. Lexical rules and contextual keywords

The preprocessor runs first and unchanged. Crug adds no preprocessing token and
no token class ([adr/0018-grammar.md](adr/0018-grammar.md) D1, D9). Every Crug
word is still a `CPP_NAME` produced by the C lexer, so:

- a macro named `own`, `move` or `go` wins at its use site; defining one
  disables the Crug reading and `-Wcrug-word-macro` warns (never promoted);
- a macro expanding to one of the words enables the Crug reading there;
- stringification and token pasting are unaffected, because the words are
  ordinary identifiers to the preprocessor;
- a diagnostic reports the macro expansion chain when the word came from one.

Several tokens are load-bearing: `<` and `>` stay separate punctuators, `>>`
stays one punctuator and may close two type-argument lists at depth two or more
in type position only, and `=>` is two tokens (`=` then `>`) recognized only in
a `match` arm. There is no `<-` and no `?` propagation operator.

The surface words and the positions in which each is recognized:

| Word | Recognized as Crug | Otherwise |
| --- | --- | --- |
| `own`, `ref`, `mut`, `raw` | a declaration specifier, before the type specifiers, in a declaration or parameter | an ordinary identifier |
| `move`, `borrow`, `borrow_mut`, `adopt`, `release`, `drop` | the callee of a call in primary-expression position, with no visible declaration of that name and the owning feature on | an ordinary call or declaration |
| `go` | the start of a statement, with `-fcrug-goroutines` | a label (`go:`), a typedef name, or any identifier |
| `yield` | `yield ( )` as a statement, with `-fcrug-goroutines` | an identifier or a call |
| `scope`, `unsafe` | a statement whose next token is `{` | a label or an identifier |
| `chan` | a type constructor in type position, with `-fcrug-channels` | a typedef name, an identifier, a function call |
| `channel`, `send`, `recv`, `close` | a call whose first argument has a channel type, or a constructor's call | the ordinary declared function, if any |
| `select` | a statement whose next token is `{` | an identifier, an assignment target, a function name |
| `match` | a statement whose next token is `(` | an identifier or call |
| `some`, `none`, `ok`, `err` | a constructor in expression position, with `-fcrug-result` | declared objects and functions |
| `option`, `result`, `slice` | a type constructor followed by `<` in type position | typedef names, identifiers |
| `enum` variants | ordinary enumerators; a parenthesised payload makes the enum a payload enum | a C enumerator |

Collision resolution, stated once: (1) with the relevant feature option off,
the C reading is the only reading; (2) a name declared in scope wins:
`int move = 3;` and `int go = 11;` compile, and `go worker ();` calls the
declared function if `go` is a function or typedef name; (3) where both
readings could parse, the C parse is attempted first and wins unless the word
is one of the fixed type or statement introducers above — `-Wcrug-go-ambiguous`
warns when a `go` call also parses as a C declaration and
`-Wcrug-keyword-shadowed` when a Crug word is shadowed; (4) labels are
untouched: `go:` is a label, and `case`/`default` keep their C meaning outside
`select`.

In kernel mode (`-fcrug-kernel`) the `go`, `yield`, `scope`, `chan` and
`select` productions are gone; the option conflicts with the concurrency
options ([FLAGS.md](FLAGS.md)).

## 2. Declarations and qualifiers

`own`, `ref`, `mut` and `raw` are **declaration specifiers**, not part of the
C type system ([adr/0018-grammar.md](adr/0018-grammar.md) D2); they belong to
the declaration-specifier list, before the type specifiers:

```c
static own char *p;     /* accepted, same as: own static char *p */
own static char *q;     /* accepted */
char *own r;            /* error: qualifier after the type specifiers */
int own;                /* a variable named own, not a qualified int */
```

Attachment follows the declarator, not the spelling. The outermost pointer
level owns; a function declarator attaches to the outermost pointer of its
return type; an array declarator attaches to the element type.

| Declaration | Meaning |
| --- | --- |
| `own T *p` | `p` owns one `T` object; the destructor has type `void (*)(T *)` |
| `own T **p` | `p` owns one pointer-sized object of type `T *`; the inner `T` is not owned |
| `own T a[4]` | error `crug-TYPE-015`: element-wise destruction is not implemented |
| `own T *g` at file scope | error `crug-OWN-014`: the owner would never be destroyed |
| `own` struct member | the containing object owns the member and destroys it |
| `typedef T *P; own P p` | `p` owns one `T`; a typedef cannot carry the qualifier |
| `own T *f (void)` | the caller owns the returned `T` |
| `void f (own T *p)` | a consuming parameter; the caller must pass `move` |
| `own int i` | error `crug-TYPE-001`: the declarator owns nothing |
| `own T (*fp)(void)` | error `crug-TYPE-005`: an owning function-pointer type |
| `own` bit-field, flexible array member, unnamed member | error `crug-TYPE-006` |
| `own T *p` in a union | error `crug-OWN-009` when the union is completed |
| `own ref T *p` | error `crug-TYPE-002`: two Crug qualifiers on one declarator |

`ref` and `mut` qualify a pointer declarator only (`crug-TYPE-007`) and may not
appear on a return type (`crug-BORROW-001`); `raw` has `own`'s placement rules.
One declarator carries at most one Crug qualifier; C qualifiers compose
(`own const char *p` is an owning pointer to `const char`, since `const` is not
`ref` and `restrict` is not `mut`). `_Atomic` may not be combined with a Crug
qualifier (`crug-TYPE-004`), and a qualifier repeating an implied fact warns
(`crug-TYPE-003`).

The qualifier is **erased from the type**: `sizeof (own T *)` equals
`sizeof (T *)`, and an owning pointer is compatible with a plain pointer in a
prototype and at a call boundary. A cast cannot create ownership, so
`(own T *) p` is an error; `raw` may be cast to. The exception is a
type-argument list: `option<own char *>` differs from `option<char *>` and
states which payload is owned.

## 3. Statements

Six statements are added: `go`, `yield` and `scope` for tasks, `select` for
channels, `match` for payload enums, and `unsafe`. All are statements, none
appears in an expression and none has a value. Grammar:
[adr/0018-grammar.md](adr/0018-grammar.md) D4-D8; tasks also in
[GOROUTINES.md](GOROUTINES.md) and [SCOPE.md](SCOPE.md).

```
go-statement     ::= "go" call ";"
call             ::= identifier "(" [ argument-list ] ")" | "(" postfix-expression ")"
yield-statement  ::= "yield" "(" ")"
scope-statement  ::= "scope" compound-statement
unsafe-statement ::= "unsafe" compound-statement
select-statement ::= "select" "{" select-arm* "}"
match-statement  ::= "match" "(" expression ")" "{" match-arm* "}"
```

**`go` and `yield`.** Form A names the callee (`go worker (job, 7);`); form B
tasks a call expression (`go ((*fp) (a));`). The prototype is checked, an owned
argument must be passed with `move`, a borrowed capture needs an enclosing
`scope`, and the task is a child of that scope; there is no detached spawn.
Both are statements, not operators: `int x = go f ();` does not parse.

**`scope` and `unsafe`.** Both take a C compound statement and are statements
only; both may be labelled. A `scope` is a task group: every exit from it
(fallthrough, `goto` out, `break` or `continue` that crosses the boundary,
`return`) waits for the tasks created inside, then runs the drops of the
block. Children of a nested `scope` belong to the nested scope. A `goto` that
jumps into a `scope`, or into the scope of an owned object, is an error
(`crug-CONC-015`, `crug-DROP-002`). An `unsafe` block is the strict-mode
escape hatch described in [UNSAFE.md](UNSAFE.md); it changes no code and does
not suspend ownership or borrow checking.

**`select`.** An arm is a `case` communication or a `default`:

```c
select {
    case v = recv (a):        /* bind a new variable, scoped to the arm */
        use (v);
    case recv (b):            /* discard the value */
        note ();
    case send (c, 7):         /* send when the channel has room */
        count ();
    default:                  /* at most one, and it must be last */
        idle ();
}
```

There is no fallthrough; an arm body is a statement list. Exactly one
`default` is allowed and must be last (`crug-CONC-005`, `crug-CONC-006`); an
empty `select { }` blocks forever; a receive binding is `case v = recv (ch)`,
never `==` (`crug-CONC-007`). No C statement starts with a block after an
identifier, so this `select` is unambiguous.

**`match`.** Arms are `pattern => body`, where `=>` is two tokens and the
body is an expression or a block:

```c
match (r) {
    ok (v)  => consume (v),
    err (e) => report (e),
}
```

The arms must be exhaustive, or the last arm must be `_`; a missing variant is
named. `default` is not an arm (use `_`). A binding is scoped to its arm; an
owned payload binding is destroyed at the end of the arm unless moved. An
expression body needs a following `,` unless the next token is `}`; a block
body is self-delimiting. Matching any other value is an error.

## 4. Types, payload enums and match

**Channels.** `chan` takes a type-name: the longest prefix that can be an
abstract declarator and does not consume a following identifier, so
`chan int *jobs` is a channel of `int *`. A pointer to a channel needs a
typedef; `chan int a[4]` is an array of channels.

**`option`, `result`, `slice`.** Recognized only in type position, because `<`
and `>` are never reinterpreted in an expression. A name opens a type-argument
list when it is one of the three words, the feature is on, and the tokens to
the matching `>` parse as type arguments followed by a declarator continuation
(`*`, identifier, `(`, `[`, `,`, `)`, `;`, `=`); this keeps `a < b` a
comparison. `slice<T>` is a borrowed view `{ T *ptr; size_t len; }` that never
owns elements; bounds checking applies to slice indexing only.

**Payload enums.** An enum is a payload enum when at least one enumerator has a
parenthesised payload:

```
payload-enum    ::= "enum" identifier? "{" payload-variant ("," payload-variant)* ","? "}"
payload-variant ::= identifier [ "(" type-name ")" ]
```

`enum Color { RED, GREEN }` stays a C enum even with `-fcrug-adt` on. `A ()`
with empty parentheses is an error (`crug-ADT-001`), a variant that needs a
payload cannot be bare (`crug-ADT-003`), and construction is call syntax,
`Text (s)` or `Quit`. Variant names are ordinary identifiers, so a function of
the same name is a C redefinition; a payload may be an owning type, owned by
the active variant.

**`match` patterns.** A pattern is `_`, a bare variant, or a variant with one
binding; the variants are `ok`, `err`, `some`, `none`, or a payload-enum
enumerator, written unqualified. At most one payload binding per pattern, an
lvalue of the payload type. An owned scrutinee must be `move (lvalue)`; the
binding then owns the payload and the rest is never dropped.

## 5. Where the grammar is defined

The normative text is [adr/0018-grammar.md](adr/0018-grammar.md) (Accepted
2026-10-18), mirrored into `spec/RFC-0001` sections 13-25 (0.2-draft).
[adr/0002-contextual-keywords.md](adr/0002-contextual-keywords.md) defines how
a Crug word is recognized without becoming a keyword; [FLAGS.md](FLAGS.md) maps
each option to the productions it turns on.

The implementation is `gcc/c/c-parser.cc` plus the frontend files it calls;
these hooks are the ones to read when this page and the code disagree:

| Production | Parser entry |
| --- | --- |
| qualifier tokens, type-name recognition | `crug_qualifier_token_kind`, `crug_qualifier_type_name_p` |
| generic type arguments, constructors | `crug_parse_generic_type`, `crug_parse_constructor` |
| payload enums and `match` | `crug_parse_match` |
| `go`, `yield`, `scope` | `crug_parse_go_statement`, `crug_parse_yield_statement`, `crug_parse_scope_statement` |
| `chan`, channel calls, `select` | `crug_parse_channel_type`, `crug_parse_channel_call`, `crug_parse_select_statement` |

The parser is the second source of truth: when it and ADR 0018 disagree the
ADR wins and the parser is fixed. One deliberate difference is recorded rather
than hidden: `yield ()` is implemented and specified in
[GOROUTINES.md](GOROUTINES.md) section 1, but is not part of ADR 0018 D4.

## 6. Tests

The grammar is covered by `gcc/testsuite/crug.dg`; each directory is run by
`crug.exp` and by a `make check-*` target.

| Suite | What it pins |
| --- | --- |
| `flags/words-as-identifiers.c`, `macro-regression.c` | every contextual word still parses as a C identifier; macros named `own`, `move`, `go`, `chan` |
| `ownership/` (12 files, `make check-OWNERSHIP`: 17 passes) | qualifier attachment, erasure, moves, the state joins |
| `borrow/` (46 files, `make check-BORROW`: 88 tests) | `ref`/`mut` declarations and all borrow diagnostics |
| `drop/` (`make check-DROP`: 39 passes) | destruction points and the drop diagnostics |
| `strict/` (11 files, `make check-STRICT_UNSAFE`: 21 tests) | `unsafe`, `raw`, and the strict policy |
| `goroutines/` (`make check-GOROUTINES`: 31 compiler tests) | `go` forms, `go:` labels, arity errors |
| `scope/` (`make check-SCOPE`: 23 compiler tests + 8 programs) | `scope`, `goto` rules, waiting and destruction order |
| `channels/` (`make check-CHANNELS`: 49 compiler tests + 8 programs) | `chan` types, `send`/`recv`/`close`, `select` arms |
| `safe-types/` (`make check-SAFE_TYPES`: 77 expected passes) | `slice`, `option`, `result` and payload enums |
| `diagnostics/` (`make check-DIAGNOSTICS`: 50 goldens) | one minimal fixture and stable code per common diagnostic |

ADR 0018's test map names `decl/`, `intrinsic/`, `generic/`, `match/`, `adt/` and
`pp/`; the consolidated suite covers them above. A new production needs a test
in a matching directory and, if it rejects something, a stable code.
