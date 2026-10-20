# Unsafe code and the strict boundary

Status: **Phases 8 and 9 implemented** (2026-10-21). This page describes
`-fcrug-strict`, the `raw` pointer qualifier and the `unsafe` block: what
each one allows, what it costs, and what it does not promise. The reference
material is [STRICT.md](STRICT.md); the decisions are
[adr/0021-strict-mode-policy.md](adr/0021-strict-mode-policy.md) for the
policy and [adr/0007-ffi-metadata-format.md](adr/0007-ffi-metadata-format.md)
for the C boundary contracts.

Ownership and borrowing are checked statically and cost nothing at run time,
which means some correct low-level code cannot be written without an escape.
Crug has exactly one escape, written in the source: `unsafe { ... }`, and one
type that states the absence of a contract, `raw`. Neither turns off the
ownership checker and neither adds a runtime check.

## 1. Strict mode

`-fcrug-strict` is a checking mode. It turns on ownership and borrowing (like
`-fcrug-borrow`) and adds seven restrictions. It changes no code: the same
source compiled with `-fcrug-ownership` and with `-fcrug-strict` produces
byte-identical object files, checked by
`scripts/check-strict-codegen.sh`.

| Operation outside an `unsafe` block | Diagnostic |
| --- | --- |
| dereference a `raw` pointer | `crug-SAFE-001` |
| convert between an integer and a pointer | `crug-SAFE-002` |
| convert an owned value to an unmanaged pointer | `crug-SAFE-003` |
| use an `asm` statement | `crug-SAFE-004` |
| call `setjmp` or `longjmp` | `crug-SAFE-005` |
| pass an owned value to an `own` parameter without `move` | `crug-OWN-011` |
| pass an owned value to a function with no ownership contract | `crug-OWN-012` |

The first five are `crug-SAFE-*` and exist only in strict mode. The two
`crug-OWN-*` restrictions are also additions of strict mode: in hybrid mode
(ownership and borrowing without `-fcrug-strict`) a call that hands an owned
value to C is not checked against a contract, because the checker has nothing
to check it against.

The policy is a fixed list, not a general warning level. `-fcrug-strict` is
not `-Wall` and does not enable unrelated warnings; it promotes exactly the
warnings whose registry row says "error under strict". The list printed by
`scripts/check-strict-policy.sh` is
`SAFE-001 SAFE-002 SAFE-003 SAFE-004 SAFE-005 OWN-011 OWN-012`.

What strict mode is for is the boundary between checked Crug and everything
else: a C library call, a function pointer from a C struct, a signal handler,
an assembler fragment. The rule is that crossing the boundary must be written
down. A plain C call that receives an owned value is not wrong; leaving the
reader to guess whether ownership was transferred is.

## 2. `raw` pointers

A `raw` pointer is a pointer with no analysis obligations. It is not owned,
it is not borrowed, and the checker never follows it. It exists so that a
pointer that is only passed through, or only used in a context the checker
cannot model, can be spelled without pretending to an ownership contract it
does not have.

```c
struct device;
struct device *open_device (const char *name);   /* plain C */
void close_device (struct device *d);

void use (void)
{
  raw struct device *d = open_device ("tty0");  /* not owned by the caller */
  if (d != 0)
    close_device (d);                            /* plain C call */
}
```

A `raw` pointer has the same size, alignment, layout and calling convention
as the corresponding plain pointer, and it can appear in a C prototype. It is
not a new representation and it does not make the pointee safe: under
`-fcrug-strict`, dereferencing it outside an `unsafe` block is
`crug-SAFE-001` and converting it to or from an integer is `crug-SAFE-002`
(section 1). The qualifier is enabled by
`-fcrug-ownership` and its placement rules are the same as `own`'s: it
qualifies the outermost pointer, is rejected on a declarator that owns nothing
(`crug-TYPE-001`), on a function type (`crug-TYPE-005`), on a bit-field,
flexible array member or unnamed member (`crug-TYPE-006`), and it may not be
combined with another Crug qualifier (`crug-TYPE-002`).

`raw` is not the same as a plain pointer. A plain `T *` is also unchecked in
hybrid mode, but it is a C type that can cross any boundary and that
`.crugffi` contracts do not see. `raw` states, in the declaration, that the
author considered the pointer and deliberately gave it no ownership meaning;
it is the spelling to use for a pointer that is only borrowed from the outside
world, or for a pointer that the code promises never to dereference except in
an `unsafe` block.

## 3. `unsafe` blocks

An `unsafe` block is a statement with a C compound statement as its body:

```c
void fill_from_fd (int fd, int n)
{
  own char *buf = malloc (n);
  unsafe {
    /* the kernel writes through buf; the transfer is checked by hand */
    if (read (fd, buf, n) < 0)
      memset (buf, 0, n);
  }
  drop (buf);                                 /* or pass it on with move */
}
```

The block is a scope for declarations and destruction, and it is the only
construct that suppresses strict-mode checks. It suppresses exactly the seven
rows of section 1: raw dereference, integer/pointer conversion, owned-to-
unmanaged conversion, `asm`, `setjmp`/`longjmp`, and the two strict call
rules. It suppresses nothing else. Inside an `unsafe` block:

- ownership still applies: a moved value stays moved, a double move is still
  `crug-UAM-002`, an unprovable destruction is still `crug-DROP-006`;
- borrowing still applies: a borrow conflict inside the block is reported;
- every other diagnostic keeps its severity, and `-Werror` keeps working;
- declarations in the block are destroyed at its end, in reverse order.

The block is not a comment and not a hint to the optimizer. It leaves no trace
in the object file, no flag on the function and no calls; a function that only
uses `unsafe` for a cast compiles to the same bytes as the cast without it.
`-fcrug-dump=ownership` and `-fcrug-dump=borrow` still describe what the
checker thinks, because the checker is still running.

Two habits make the block safe to review. First, keep it as small as the
unchecked operation: one dereference, one cast, one call. Second, write down
what is being promised, as the comment does above. The block is not evidence
that the code is safe; it is evidence that the author knew the operation was
unchecked.

## 4. Ownership at boundaries

Strict mode makes the boundary explicit in both directions.

**Into a callee.** An owned value passed to an `own` parameter must be moved:
`take (move (p));` transfers the value, and the callee then owns it and is
responsible for destroying or releasing it. Passing `p` directly is
`crug-OWN-011`, whose message names the fix. An owned value
passed to an unannotated function is `crug-OWN-012`: the callee does not know
it received ownership, so the caller must either state the contract or hand
over an unmanaged pointer. There are three ways to do that, in increasing
order of visibility:

```c
void consume (void *p);                  /* plain C, no contract */

void use (void)
{
  own char *p = malloc (16);

  consume (p);                           /* error crug-OWN-012 under strict */
  unsafe { consume (p); }                /* accepted: "I know what this does" */
  release (p);                           /* accepted: ownership ends here */
}
```

`release (p)` is the checked form: it ends the ownership, so the variable
owns nothing afterwards and nothing is destroyed at the end of the scope. The
`unsafe` block is the unchecked form: the value stays owned, and the callee
must not free it unless the author also arranges the hand-off.

**Out of a callee.** A `.crugffi` contract with `returns owned T` and
`drop D` makes the call's result owned and adopts it with `D`
(`own FILE *f = fopen (...);`). Without a contract, an owned declaration
must take its pointer from `adopt (raw, dtor)` or from the allocation
catalogue; a plain function's return has an unknown destructor and is reported
by `-Wcrug-unadopted-own` (`crug-OWN-007`). An `out_owned` parameter
initializes an owned variable that is empty before the call
(`crug-FFI-008`), and the variable is destroyed at its scope end with the
contract's destructor. A
contract's `consumes argN` is the same ownership transfer as an `own`
parameter, checked at the call site (`crug-FFI-007`); its `borrows` and
`mut_borrows` clauses are the FFI spelling of `ref` and `mut`.

**The discarded `adopt`.** Writing `adopt (raw, dtor);` as a statement
leaves the owning result on the floor and is `crug-OWN-013` in every mode,
strict or not. The result must initialize an owned declaration:
`own T *p = adopt (raw, dtor);`. There is no "adopt into an existing owner";
assignment is through `move` or `release` followed by a new declaration.

A `.crugffi` contract is not a comment: it supplies the destructor to adopt
and satisfies the strict call policy for that call, so a program with complete
contracts can pass `-fcrug-strict` without `unsafe` at the boundary. The
contract format and the clause semantics are in [FFI.md](FFI.md); the thread
affinity, `Send` and `Sync` consequences are in [C-FFI.md](C-FFI.md).

## 5. Diagnostics

The strict and unsafe diagnostics are errors with a stable code; the option
column is the registry's spelling, and the severity is the same in default and
strict mode unless the row says otherwise.

| Code | Trigger | Option |
| --- | --- | --- |
| `crug-SAFE-001` | dereference of a `raw` pointer outside `unsafe` | - |
| `crug-SAFE-002` | integer/pointer conversion outside `unsafe` | - |
| `crug-SAFE-003` | an owned value converted to an unmanaged pointer | - |
| `crug-SAFE-004` | `asm` statement outside `unsafe` | - |
| `crug-SAFE-005` | `setjmp`/`longjmp` outside `unsafe` under strict | - |
| `crug-OWN-011` | an owned value passed to an `own` parameter without `move` | - |
| `crug-OWN-012` | an owned value passed to a callee with no ownership contract | - |
| `crug-OWN-013` | the result of `adopt` is discarded | - |
| `crug-TYPE-001` | `raw` on a declarator that owns nothing | `-Wcrug-qualifier-placement` |
| `crug-TYPE-002` | `raw` combined with another Crug qualifier | `-Wcrug-qualifier-combination` |
| `crug-TYPE-003` | redundant qualifier | `-Wcrug-qualifier-redundant` (warning) |
| `crug-TYPE-005`, `crug-TYPE-006` | `raw` on a function type; on a bit-field, flexible array member or unnamed member | `-Wcrug-qualifier-placement` |

`crug-OWN-013` is an ownership error in every mode, not a strict-mode
diagnostic; `crug-TYPE-003` warns by default and is an error under strict.

## 6. What unsafe does not make safe

The block is an escape from checking, not a proof of correctness. It does not
give any of the following:

- **Memory safety.** A dereference inside `unsafe` is the C dereference it
  looks like: a dangling, misaligned or out-of-bounds pointer is still a bug.
- **Provenance or aliasing facts.** The block suppresses the check; it does not
  tell the optimizer anything, and `-fcrug-alias-facts` does not read it.
- **Sanitizer coverage.** No AddressSanitizer, UndefinedBehaviorSanitizer or
  ThreadSanitizer instrumentation is added or changed by Crug; the host
  compiler's sanitizers apply to the generated code as usual
  ([FUZZING.md](FUZZING.md)).
- **Release of ownership.** Moving, dropping or releasing inside an
  `unsafe` block still updates the state machine, and leaving the block with a
  value owned is still the state the checker sees at the end of the scope.
- **Lifetime extension.** A borrow used after its last use, or after the
  referent dies, is not made valid by the block; the block suppresses the
  strict checks in section 1, not the borrow rules.
- **Race freedom.** `unsafe` says nothing about threads; the `Send`/`Sync`
  rules for tasks and channels are separate and are not waived
  ([SEND-SYNC.md](SEND-SYNC.md)).
- **Permission for the kernel mode.** `-fcrug-kernel` rejects ownership and
  borrowing in this release; the kernel escape policy is not implemented
  ([adr/0017-kernel-ownership-policy.md](adr/0017-kernel-ownership-policy.md)).

The mental model: outside `unsafe` the checker has looked at the code; inside
it, the author has. A reviewer of an `unsafe` block reviews the C.

## 7. Tests

| Check | What it proves |
| --- | --- |
| `gcc/testsuite/crug.dg/strict/` (11 files, 21 DejaGnu tests) and `strict/clean/` (4 programs) | one rejection per code, the hybrid-mode counterpart of each, two `dg-do run` programs, and clean programs whose object bytes match |
| `make check-STRICT_UNSAFE` | runs the 21 tests; the suite prints `strict-suite: 21 tests, 0 failures` |
| `scripts/check-strict-raw.sh` | hybrid mode accepts a raw dereference, strict rejects it with `crug-SAFE-001` |
| `scripts/check-strict-codegen.sh` | strict and ownership objects are byte-identical |
| `scripts/check-strict-policy.sh` | the promoted-warning list is exactly the seven codes |
| `make check-FFI` (19 cases) | a `.crugffi` contract satisfies the strict call policy and supplies destructors |
| `gcc/testsuite/crug.dg/diagnostics/` | the common-error fixtures for `SAFE-001`..`SAFE-005` and `OWN-011`/`012` |

Examples are `examples/08-strict-mode.c` and `examples/09-unsafe-asm.c`; the
phase-8 gate is in [EXIT-GATES.md](EXIT-GATES.md) phase 8 and the FFI gate in
phase 9. A new strict check needs a test in `strict/`, a row in
[DIAGNOSTICS.md](DIAGNOSTICS.md) and, if promoted, an entry in
`check-strict-policy.sh`.
