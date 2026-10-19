# CRUG Diagnostic Code Registry

Status: proposed, 2026-10-18. This file is the canonical registry of stable
diagnostic codes. ADR 0018 D10 is canonical for the `-Wcrug-*` options and for
strict-mode promotion; this file is canonical for the code that names each
diagnostic. Where the two disagree, the disagreement is a defect to be fixed in
both files in one commit.

## 1. Why stable codes

A diagnostic has three parts with three different lifetimes:

1. the **condition** - semantic, changes only with a language decision;
2. the **code** - `crug-<AREA>-<NNN>`, permanent once allocated;
3. the **wording** - may improve at any time, but only with its golden file.

Tests, golden files, documentation, and the JSON consumer in EXIT-GATES phase 20
refer to the code, never to the wording. The phase-4 gate requires the
use-after-move error to carry `[crug-UAM-001]`; phase 20 requires every
machine-readable diagnostic to carry a code and notes. A code is therefore part
of the interface, and the rules below are the price of keeping it stable.

## 2. Format and stability rules

```
crug-<AREA>-<NNN>
```

- `AREA` is one of the areas in section 3. The area is part of the code;
  moving a diagnostic between areas is a new code.
- `NNN` is three digits, `001`-`099`. Numbers are never reused, never
  renumbered, and never sorted for aesthetics: the next free number wins.
- A retired diagnostic keeps its row, marked `retired in <version>`, and its
  number stays reserved forever. It is not deleted, because old logs and issue
  reports still quote it.
- A code is allocated only together with (a) its registry row, (b) its origin
  decision, and (c) at least one test in `gcc/testsuite/crug.dg/diagnostics/`
  named after the code. A code without a test is a wish, not a diagnostic.
- No diagnostic fires with all Crug flags off (ADR 0018 D1). Every code below is
  therefore reachable only under the flag that enables its construct.
- The human-readable form is
  `<file>:<line>:<column>: error: <message> [-Wcrug-<option>] [crug-<AREA>-<NNN>]`,
  with the option bracket omitted for hard errors that have no option.
- The machine-readable form (GCC `-fdiagnostics-format=json`) carries the code
  as the string member `code` and keeps the related-location chain in `notes`.
  Codes are identical in both forms; there is no numeric-only alternative.

## 3. Areas

| Area | Scope | Range | Owning decision |
|------|-------|-------|-----------------|
| `UAM` | move paths and use-after-move | 001-099 | ADR 0004 |
| `OWN` | ownership operands, adoption, release, transfer | 001-099 | ADR 0004, ADR 0018 D3 |
| `BORROW` | borrow conflicts, escapes, returned borrows | 001-099 | MASTER_SPEC s10, RFC-0001 B1 |
| `DROP` | drop insertion, jump policy, setjmp policy | 001-099 | ADR 0005 |
| `CONC` | goroutines, channels, select | 001-099 | ADR 0018 D4-D6 |
| `ADT` | payload enums and match | 001-099 | ADR 0018 D8 |
| `TYPE` | declarator formation, qualifiers, generic types | 001-099 | ADR 0018 D2, D7 |
| `GRAMMAR` | parsing, keyword and intrinsic shadowing, macros | 001-099 | ADR 0002, ADR 0018 D9 |
| `SAFE` | strict mode: raw dereference, pointer casts, unsafe blocks, asm, non-local exits | 001-099 | ADR 0021 |
| `FFI` | `.crugffi` metadata and boundary checks | 001-099 | ADR 0007 (implemented; FFI-001 to FFI-011 allocated) |
| `KERNEL` | freestanding and kernel-mode restrictions | 001-099 | ADR 0016, ADR 0017 (unwritten) |

Reserved areas have no allocated codes yet. ADR 0007 now exists, so `FFI` codes may
be allocated when the Phase 9 parser lands; `KERNEL` still waits for ADR 0016 and
ADR 0017. Allocating one before its owning ADR
exists is refused by review, because the code would encode semantics nobody has
decided.

## 4. Registry

Columns: **Code**; **Option** (`-` when the error has no `-W` option); **Default**
severity in hybrid mode; **Strict** severity under `-fcrug-strict`; **Canonical
message**, with `%s` standing for a name or type; **Origin** decision.

### 4.1 Move paths (`UAM`)

| Code | Option | Default | Strict | Canonical message | Origin |
|------|--------|---------|--------|-------------------|--------|
| `crug-UAM-001` | - | error | error | use of moved value '%s' | ADR 0003 D2 |
| `crug-UAM-002` | - | error | error | move of already-moved value '%s' | ADR 0003 D2 |
| `crug-UAM-003` | - | error | error | implicit copy of owned value '%s'; owned values are not copyable | RFC-0001 O1, ADR 0003 D2 |
| `crug-UAM-004` | - | error | error | use of possibly-moved value '%s' after a branch join | ADR 0003 D2 |
| `crug-UAM-005` | - | error | error | partially moved aggregate '%s' used as a whole | GAPS s5, ADR 0004 D4 (post-MVP) |
| `crug-UAM-006` | - | error | error | move of aggregate field '%s' is not supported; move the whole object | ADR 0004 D4 |
| `crug-UAM-007` | `-Wcrug-owned-byte-copy` | warning | error | byte copy of owned value '%s' can duplicate ownership | ADR 0004 D9 |

### 4.2 Ownership operands (`OWN`)

| Code | Option | Default | Strict | Canonical message | Origin |
|------|--------|---------|--------|-------------------|--------|
| `crug-OWN-001` | `-Wcrug-intrinsic-operand` | error | error | move() operand is not an owned object | ADR 0018 D3 |
| `crug-OWN-002` | `-Wcrug-adopt-signature` | error | error | adopt() destructor must have type void (*)(T *) for the operand type T | ADR 0018 D3 |
| `crug-OWN-003` | `-Wcrug-cast-cannot-own` | error | error | cast cannot create ownership; use adopt() | ADR 0018 D2 |
| `crug-OWN-004` | `-Wcrug-intrinsic-operand` | error | error | release() operand is not an owned object | ADR 0018 D3 |
| `crug-OWN-005` | - | error | error | cannot create an owned value from a non-owned expression; use adopt() | ADR 0018 D2 |
| `crug-OWN-006` | `-Wcrug-conditional-ownership` | error | error | conditional expression mixes owned and non-owned operands | ADR 0018 D3 |
| `crug-OWN-007` | `-Wcrug-unadopted-own` | warning | error | owned value '%s' is never adopted or released | ADR 0004 D8 |
| `crug-OWN-008` | `-Wcrug-ffi-ownership` | warning | error | owned value crosses an FFI boundary without adopt() or release() metadata | ADR 0003 D3 |
| `crug-OWN-009` | - | error | error | own member in union requires an active-variant contract | ADR 0004 D6 |
| `crug-OWN-010` | - | error | error | conflicting destructors for owned value '%s' on different paths | ADR 0005 A-6 |
| `crug-OWN-011` | - | error | error | an owned value passed to an 'own' parameter must be moved explicitly: write move (x) | ADR 0021 D6 |
| `crug-OWN-012` | - | error | error | an owned value passed to a callee without an ownership contract; release (x) first, or use an unsafe block | ADR 0021 D6 |
| `crug-OWN-013` | - | error | error | the result of adopt() is discarded; initialize an owned declaration with it | ADR 0004 D10 |

### 4.3 Borrowing (`BORROW`)

| Code | Option | Default | Strict | Canonical message | Origin |
|------|--------|---------|--------|-------------------|--------|
| `crug-BORROW-001` | `-Wcrug-returned-borrow` | error | error | a function cannot return a borrow | ADR 0018 D2 |
| `crug-BORROW-002` | - | error | error | cannot mutably borrow '%s' while it is shared-borrowed | MASTER_SPEC s10, RFC-0001 B1 |
| `crug-BORROW-003` | - | error | error | cannot mutably borrow '%s' more than once | MASTER_SPEC s10, RFC-0001 B1 |
| `crug-BORROW-004` | - | error | error | cannot move '%s' while it is borrowed | MASTER_SPEC s10 |
| `crug-BORROW-005` | - | error | error | cannot drop '%s' while it is borrowed | MASTER_SPEC s10 |
| `crug-BORROW-006` | `-Wcrug-intrinsic-operand` | error | error | borrow() operand must be an lvalue | ADR 0018 D3 |
| `crug-BORROW-007` | - | error | error | borrow '%s' escapes the lifetime of its referent | MASTER_SPEC s10 |
| `crug-BORROW-008` | - | error | error | assignment through shared borrow '%s' | MASTER_SPEC s10 |
| `crug-BORROW-009` | - | error | error | borrow() operand must be a whole owned object or a borrow; member borrows are not supported in this phase | ADR 0018 D3, Phase 6 |
| `crug-BORROW-010` | - | error | error | a mutable borrow of a borrow, or of a non-owned object, is not supported in this phase | ADR 0018 D3, Phase 6 |
| `crug-BORROW-011` | - | error | error | cannot borrow '%s' because it owns nothing | ADR 0005 A-5, Phase 6 |
| `crug-BORROW-012` | - | error | error | a mutable borrow cannot be initialized from a shared borrow | ADR 0018 D2, Phase 6 |
| `crug-BORROW-013` | - | error | error | borrow '%s' is used without being initialized on this path | ADR 0018 D3, Phase 6 |
| `crug-BORROW-014` | - | error | error | cannot assign to '%s' while it is borrowed | MASTER_SPEC s10, Phase 6 |
| `crug-BORROW-015` | - | error | error | cannot use '%s' while it is mutably borrowed | MASTER_SPEC s10, Phase 6 |
| `crug-BORROW-016` | - | error | error | cannot borrow '%s' while it is mutably borrowed | MASTER_SPEC s10, Phase 6 |
| `crug-BORROW-017` | - | error | error | borrow '%s' must be initialized from borrow(), borrow_mut() or another borrow | ADR 0018 D3, Phase 6 |
| `crug-BORROW-018` | - | error | error | a borrow must be an automatic variable; it cannot outlive its referent | ADR 0019 D3, Phase 7 |
| `crug-BORROW-019` | - | error | error | a borrow cannot be returned from the function that created it | ADR 0019 D4, Phase 7 |
| `crug-BORROW-020` | - | error | error | a borrow may only be passed to a 'ref' or 'mut' parameter | ADR 0019 D2, Phase 7 |
| `crug-BORROW-021` | - | error | error | passing a plain pointer to a 'mut' parameter proves no exclusive access; pass borrow_mut (...) or a mutable borrow | ADR 0010 D4, Phase 11 |
| `crug-BORROW-022` | - | error | error | a shared borrow cannot be passed to a 'mut' parameter | ADR 0010 D4, Phase 11 |
| `crug-BORROW-023` | - | error | error | the lender '%s' is also passed to this call; a 'mut' parameter must be the only view of its object | ADR 0010 D4, Phase 11 |

### 4.4 Destruction (`DROP`)

| Code | Option | Default | Strict | Canonical message | Origin |
|------|--------|---------|--------|-------------------|--------|
| `crug-DROP-001` | `-Wcrug-intrinsic-operand` | error | error | drop() operand is not an owned object | ADR 0018 D3 |
| `crug-DROP-002` | `-Wcrug-jump-into-scope` | error | error | goto jumps into the scope of owned '%s' | ADR 0005 D3 |
| `crug-DROP-003` | `-Wcrug-setjmp-drops` | warning | error | destructors between setjmp and longjmp may be skipped for '%s' | ADR 0005 D3 |
| `crug-DROP-004` | - | error | error | owned value '%s' may be destroyed twice on this path | ADR 0005 D4 |
| `crug-DROP-005` | `-Wcrug-drop-unreachable` | warning | error | owned value '%s' can never be dropped on any path | ADR 0005 D8 |
| `crug-DROP-006` | - | error | error | cannot prove that '%s' is destroyed exactly once on this path | ADR 0005 A-6 |
| `crug-DROP-007` | - | error | error | owned value '%s' may be used before it is initialized | ADR 0005 A-5 |
| `crug-DROP-008` | - | error | error | assignment to an owned value that already owns; call drop() or release() first | ADR 0005 A-4 |

### 4.5 Concurrency (`CONC`)

| Code | Option | Default | Strict | Canonical message | Origin |
|------|--------|---------|--------|-------------------|--------|
| `crug-CONC-001` | `-Wcrug-channel-shape` | error | error | channel element is not a type-name | ADR 0018 D6 |
| `crug-CONC-002` | `-Wcrug-channel-shape` | error | error | channel(n) requires a channel type from context | ADR 0018 D6 |
| `crug-CONC-003` | - | error | error | send transfers ownership; wrap the argument in move() | ADR 0018 D6 R2 |
| `crug-CONC-004` | `-Wcrug-select-form` | error | error | select case must be 'case v = recv(ch):', 'case send(ch, v):' or 'default:' | ADR 0018 D6 R4 |
| `crug-CONC-005` | `-Wcrug-select-form` | error | error | select may have at most one default | ADR 0018 D6 R4 |
| `crug-CONC-006` | `-Wcrug-select-form` | error | error | default must be the last select arm | ADR 0018 D6 R4 |
| `crug-CONC-007` | `-Wcrug-select-form` | error | error | 'case v == recv(ch)' is not a select form; use 'case v = recv(ch)' | ADR 0018 D6 R4 |
| `crug-CONC-008` | - | error | error | 'go' callee must return void | ADR 0018 D4 |
| `crug-CONC-009` | - | error | error | select may not be used as an expression | ADR 0018 D6 R5 |
| `crug-CONC-010` | - | error | error | 'go' requires a function | ADR 0018 D4, Phase 14 |
| `crug-CONC-011` | - | error | error | owned value passed to concurrent task without ownership transfer | ADR 0018 D4, MASTER_SPEC 27, Phase 14 |
| `crug-CONC-012` | - | error | error | a borrow cannot be captured by a concurrent task; capture an owned value with move(...) | ADR 0018 D4, Phase 14 |
| `crug-CONC-013` | - | error | error | 'go' requires a prototype for its callee | ADR 0018 D4, Phase 14 |
| `crug-CONC-014` | - | error | error | a destructor cannot cross a task boundary; the task destroys an owned parameter by itself | ADR 0008 D3, Phase 14 |
| `crug-CONC-015` | - | error | error | goto jumps into a scope block | ADR 0024 D5, Phase 15 |
| `crug-CONC-016` | - | error | error | a scoped task borrows an object from a block that ends before the scope; declare it in the scope body | ADR 0024 D4, Phase 15 |
| `crug-CONC-017` | `-Wcrug-select-form` | error | error | a 'select' arm cannot receive from a channel with an owning element; use 'recv' directly | ADR 0025 D4, Phase 16 |
| `crug-CONC-018` | - | error | error | the first argument of '%s' must be a channel | ADR 0025 D1, Phase 16 |
| `crug-CONC-019` | - | error | error | a channel element must be a complete type with a size known at compile time | ADR 0025 D1, Phase 16 |
| `crug-CONC-020` | - | error | error | 'channel' takes one argument: the buffer capacity | ADR 0025 D1, Phase 16 |
| `crug-CONC-021` | - | error | error | 'send' takes two arguments: send (channel, value) | ADR 0025 D1, Phase 16 |
| `crug-CONC-022` | - | error | error | '%s' takes one argument | ADR 0025 D1, Phase 16 |
| `crug-CONC-023` | - | error | error | a destructor cannot cross a channel; the receiver of the value destroys it | ADR 0025 D4, Phase 16 |

### 4.6 Payload enums and match (`ADT`)

| Code | Option | Default | Strict | Canonical message | Origin |
|------|--------|---------|--------|-------------------|--------|
| `crug-ADT-001` | `-Wcrug-adt-form` | error | error | empty payload parentheses are not allowed; omit the parentheses for a unit variant | ADR 0018 D8 |
| `crug-ADT-002` | `-Wcrug-adt-form` | error | error | a payload variant takes exactly one type-name | ADR 0018 D8 |
| `crug-ADT-003` | - | error | error | variant '%s' expects a payload | ADR 0018 D8 |
| `crug-ADT-004` | - | error | error | variant '%s' does not take a payload | ADR 0018 D8 |
| `crug-ADT-005` | - | error | error | '%s' needs an option or result type from its context; initialize a declaration or use it in a return statement | ADR 0018 D8, Phase 10 |
| `crug-ADT-006` | - | error | error | '%s' does not construct '%s' | ADR 0018 D8, Phase 10 |
| `crug-ADT-010` | `-Wcrug-match-form` | error | error | match arm must be 'pattern => expression' or 'pattern => { ... }' | ADR 0018 D8 |
| `crug-ADT-011` | `-Wcrug-match-non-exhaustive` | error | error | match on '%s' is not exhaustive; add the missing variant or '_' | ADR 0018 D8 |
| `crug-ADT-012` | `-Wcrug-match-unreachable` | warning | error | match arm '%s' is unreachable | ADR 0018 D8 |
| `crug-ADT-013` | `-Wcrug-match-form` | error | error | match requires an option, result or payload enum value | ADR 0018 D8 |
| `crug-ADT-014` | `-Wcrug-match-form` | error | error | match may not be used as an expression; assign inside the arms | ADR 0018 D8 |
| `crug-ADT-015` | `-Wcrug-match-form` | error | error | an owned scrutinee must be moved into match: write match (move(x)) | ADR 0018 D8 |
| `crug-ADT-016` | - | error | error | a payload enum cannot have a fixed underlying type | ADR 0018 D8, Phase 10 |
| `crug-ADT-017` | - | error | error | redefinition of 'enum %s' as a payload enum; define the payload enum before any reference to its tag | ADR 0018 D8, Phase 10 |
| `crug-ADT-018` | - | error | error | an owning payload enum cannot be passed by value: the destructor of its payload is not part of the ABI; keep the value in one function or pass a pointer | ADR 0018 D8, ADR 0004 D4, Phase 10 |
| `crug-ADT-019` | - | error | error | the payload of owned variant '%s' must be moved in, adopted, or a fresh allocation; write move (...), adopt (...) or an allocation call | ADR 0018 D8, ADR 0004 D4, Phase 10 |
| `crug-ADT-020` | `-Wcrug-match-form` | error | error | an arm covering owned variant '%s' must bind its payload; a '_' arm cannot cover an owned variant that was not named | ADR 0018 D8 R4, Phase 10 |

### 4.7 Types and declarators (`TYPE`)

| Code | Option | Default | Strict | Canonical message | Origin |
|------|--------|---------|--------|-------------------|--------|
| `crug-TYPE-001` | `-Wcrug-qualifier-placement` | error | error | qualifier is not allowed on this declarator | ADR 0018 D2 |
| `crug-TYPE-002` | `-Wcrug-qualifier-combination` | error | error | more than one Crug qualifier on one declarator | ADR 0018 D2 |
| `crug-TYPE-003` | `-Wcrug-qualifier-redundant` | warning | error | redundant qualifier '%s' | ADR 0018 D2 |
| `crug-TYPE-004` | `-Wcrug-qualifier-combination` | error | error | '_Atomic' may not be combined with a Crug qualifier | ADR 0018 D2 |
| `crug-TYPE-005` | `-Wcrug-qualifier-placement` | error | error | 'own' may not qualify a function type | ADR 0018 D2 |
| `crug-TYPE-006` | `-Wcrug-qualifier-placement` | error | error | qualifier is not allowed on a bit-field, flexible array member or unnamed member | ADR 0018 D2 |
| `crug-TYPE-007` | `-Wcrug-qualifier-placement` | error | error | a borrow qualifier must apply to a pointer declarator | ADR 0018 D2, Phase 6 |
| `crug-TYPE-010` | `-Wcrug-generic-form` | error | error | generic type is not allowed in this position | ADR 0018 D7 |
| `crug-TYPE-011` | `-Wcrug-generic-form` | error | error | '%s<...>' is not a generic type; unknown type constructor | ADR 0018 D7 |
| `crug-TYPE-012` | - | error | error | '%s' needs a pointer as its first argument | ADR 0018 D7, Phase 10 |
| `crug-TYPE-013` | `-Wcrug-generic-form` | error | error | '%s' needs exactly %d type argument(s) | ADR 0018 D7, Phase 10 |
| `crug-TYPE-014` | `-Wcrug-generic-form` | error | error | expected '>' to close the type argument list | ADR 0018 D7, Phase 10 |

### 4.8 Parsing and shadowing (`GRAMMAR`)

| Code | Option | Default | Strict | Canonical message | Origin |
|------|--------|---------|--------|-------------------|--------|
| `crug-GRAMMAR-001` | - | error | error | cannot take the address of a move() or borrow() result | ADR 0018 D3 |
| `crug-GRAMMAR-002` | `-Wcrug-keyword-shadowed` | warning | error | Crug word '%s' is shadowed by a macro | ADR 0002 D8 |
| `crug-GRAMMAR-003` | `-Wcrug-intrinsic-shadowed` | warning | error | Crug intrinsic '%s' is shadowed by a user declaration | ADR 0018 D3 |
| `crug-GRAMMAR-004` | `-Wcrug-go-ambiguous` | warning | error | 'go' is ambiguous here; write 'go (expr);' or rename the declaration | ADR 0018 D4 |
| `crug-GRAMMAR-005` | `-Wcrug-word-macro` | warning | warning | macro definition redefines the Crug word '%s' | ADR 0018 D9 |
| `crug-GRAMMAR-006` | - | error | error | 'scope', 'unsafe', 'match' and 'select' require a compound statement | ADR 0018 D5, D6, D8 |

### 4.9 Strict mode, raw and unsafe (`SAFE`)

| Code | Option | Default | Strict | Canonical message | Origin |
|------|--------|---------|--------|-------------------|--------|
| `crug-SAFE-001` | - | error | error | dereference of 'raw' pointer '%s' outside an 'unsafe' block | ADR 0021 D2 |
| `crug-SAFE-002` | - | error | error | conversion between an integer and a pointer outside an 'unsafe' block | ADR 0021 D3 |
| `crug-SAFE-003` | - | error | error | an owned value cannot be converted to an unmanaged pointer; release (x) first, or use an unsafe block | ADR 0021 D4 |
| `crug-SAFE-004` | - | error | error | an 'asm' statement outside an 'unsafe' block can change memory that Crug tracks | ADR 0021 D5 |
| `crug-SAFE-005` | - | error | error | 'setjmp' bypasses automatic destruction; call it inside an 'unsafe' block under '-fcrug-strict' | ADR 0021 D6 |

Note: these five are strict-only diagnostics; in hybrid mode the construct is
accepted (with the DROP-003 warning for setjmp). Their codes are stable, so a
driver can tell a strict rejection from any other error.

### 4.10 C FFI contracts (`FFI`)

Codes in this area are emitted while reading `.crugffi` metadata and while
checking a call against a contract (ADR 0007). A sidecar error names the
metadata file and line; a boundary error names the call site and carries the
sidecar location in a note.

| Code | Option | Hybrid | Strict | Message | Origin |
| --- | --- | --- | --- | --- | --- |
| `crug-FFI-001` | - | error | error | malformed sidecar: missing `crugffi <major>.<minor>;` header, unreadable file, missing `;`, unexpected character, or an unknown declaration keyword | ADR 0007 D2, D6 |
| `crug-FFI-002` | - | error | error | `type void` is not a valid declaration | ADR 0007 D2 |
| `crug-FFI-003` | - | error | error | unknown or repeated clause, or a malformed `argN`/`ret` operand | ADR 0007 D2, D3 |
| `crug-FFI-004` | - | error | error | conflicting declarations: two sidecars disagree about the same function or type, or one operand carries conflicting clauses | ADR 0007 D4 |
| `crug-FFI-005` | - | error | error | argument index out of range for the declared prototype | ADR 0007 D3 |
| `crug-FFI-006` | - | error | error | the declaration does not fit the metadata: function not declared in the translation unit, not external, no prototype, argument not a pointer, `out_owned` needs pointer-to-pointer, `callback` needs a function pointer, return type mismatch, or no destructor can be determined | ADR 0007 D3, D5 |
| `crug-FFI-007` | - | error | error | a `consumes` argument is not moved: pass `move` of an owned value, a fresh allocation, or null | ADR 0007 D3 |
| `crug-FFI-008` | - | error | error | an `out_owned` argument is not the address of an owned variable, or that variable already owns a value | ADR 0007 D3 |
| `crug-FFI-009` | - | error | error | a `callback` argument is neither a function nor a null pointer | ADR 0007 D3 |
| `crug-FFI-010` | - | error | error | the metadata file exceeds 1 MiB or declares more than 10,000 entries | ADR 0007 D7 |
| `crug-FFI-011` | - | error | error | a destructor named by `drop` cannot be resolved: not an external function, not one pointer argument, or the wrong pointee type | ADR 0007 D3 |

All eleven are errors in both modes: they describe metadata that cannot be
honoured, not a style choice. Phase 9 implements every code in this table.

## 5. Option map

Several codes share one option; the code identifies the condition, the option
controls the warning group. This table is the reverse index of section 4, and
must be updated in the same commit as the registry rows.

| Option | Codes |
|--------|-------|
| `-Wcrug-owned-byte-copy` | UAM-007 |
| `-Wcrug-intrinsic-operand` | OWN-001, OWN-004, BORROW-006, DROP-001 |
| `-Wcrug-adopt-signature` | OWN-002 |
| `-Wcrug-cast-cannot-own` | OWN-003 |
| `-Wcrug-conditional-ownership` | OWN-006 |
| `-Wcrug-unadopted-own` | OWN-007 |
| `-Wcrug-ffi-ownership` | OWN-008 |
| `-Wcrug-returned-borrow` | BORROW-001 |
| `-Wcrug-jump-into-scope` | DROP-002 |
| `-Wcrug-setjmp-drops` | DROP-003 |
| `-Wcrug-drop-unreachable` | DROP-005 |
| `-Wcrug-channel-shape` | CONC-001, CONC-002 |
| `-Wcrug-select-form` | CONC-004, CONC-005, CONC-006, CONC-007, CONC-017 |
| `-Wcrug-adt-form` | ADT-001, ADT-002 |
| `-Wcrug-match-form` | ADT-010, ADT-013, ADT-014, ADT-015, ADT-020 |
| `-Wcrug-match-non-exhaustive` | ADT-011 |
| `-Wcrug-match-unreachable` | ADT-012 |
| `-Wcrug-qualifier-placement` | TYPE-001, TYPE-005, TYPE-006 |
| `-Wcrug-qualifier-combination` | TYPE-002, TYPE-004 |
| `-Wcrug-qualifier-redundant` | TYPE-003 |
| `-Wcrug-generic-form` | TYPE-010, TYPE-011 |
| `-Wcrug-keyword-shadowed` | GRAMMAR-002 |
| `-Wcrug-intrinsic-shadowed` | GRAMMAR-003 |
| `-Wcrug-go-ambiguous` | GRAMMAR-004 |
| `-Wcrug-word-macro` | GRAMMAR-005 |

`-fcrug-strict` promotes exactly the rows whose Default is `warning` and whose
Strict is `error`, and nothing else. `-Wcrug-word-macro` stays a warning even
in strict mode because the macro is already tokenized by then and no reading is
unsafe, only surprising.

## 6. Message rules

- The message names the **source construct** and the **variable, type, or
  variant** involved. It never names a pass, an IR object, a dump name or a
  sanitizer.
- A rejected operand message ends with the operand type only when the type
  explains the rejection, for example `move() operand is not an owned object`.
- `note:` lines carry the fix: the flag, the intrinsic to use, or the exact
  spelling. The phase-20 gate rejects any fix instruction that requires reading a
  compiler dump.
- Wording changes are allowed; they must update the golden file in the same
  commit. Code changes are not allowed without a new number.
- Dynamic text (`%s`) is quoted with straight single quotes; no backticks, no
  angle brackets, no nesting quotes. This keeps `grep`-based consumers and
  golden diffs stable.

## 7. Allocation and retirement

1. Pick the next free number in the area that owns the semantics (section 3),
   not the area of the flag that reports it.
2. Add the registry row with origin decision, default and strict severity.
3. Add `gcc/testsuite/crug.dg/diagnostics/<code>.c` with `// { dg-error
   "<canonical message>" }` and the code in a comment; the test must fail on
   the code, not only on the wording.
4. Add the option to ADR 0018 D10 if it is new, and to section 5 above.
5. Run `scripts/check-diagnostics.sh`; it fails if a referenced code is
   missing, a row is duplicated, or an area is unknown.

Retirement: keep the row, change Default and Strict to `retired`, and state the
version. The number is never reused. A retired code may still appear in archived
gate logs.

## 8. Verification

`scripts/check-diagnostics.sh` checks the mechanical properties:

- every code in `docs/` and `spec/` is registered here;
- every registry row has a unique code and a known area;
- no code is registered twice.

It does not check semantics; that is the gate's job. The phase-4 gate runs the
script and requires `[crug-UAM-001]` on the use-after-move test; phase 20 runs
the script plus the 50-entry golden corpus that this registry seeds. The registry
is therefore the *index* of diagnostics, not the proof that they work: a code
with a row and no test is caught by review, a code with a test and no row is
caught by the script.

## 9. Status

125 codes are registered. The count per area is: UAM 7, OWN 13, BORROW 23,
DROP 8, CONC 23, ADT 17, TYPE 12, GRAMMAR 6, SAFE 5, FFI 11.

Phase 16 added seven codes, all in ADR 0025: CONC-017 (a 'select' arm cannot
receive from a channel with an owning element), CONC-018 (a channel operation
whose first argument is not a channel), CONC-019 (an element type with no size
known at compile time), CONC-020 (channel arity), CONC-021 (send arity),
CONC-022 (recv/close arity) and CONC-023 (an adopted destructor crossing a
channel). CONC-001 to CONC-009, reserved for this phase, are implemented as
well, so the whole CONC area is now live.

Phase 15 added two codes: CONC-015 (a goto into a scope block) and CONC-016
(a scoped task borrowing an object whose block ends before the scope does);
the scoped-borrow conflicts reuse BORROW-002 and BORROW-003 at the scope.

Phase 14 added five codes: CONC-010 ('go' requires a function), CONC-011 (an
owned value captured without ownership transfer), CONC-012 (a borrow captured
by a task), CONC-013 ('go' requires a prototype) and CONC-014 (a destructor
crossing a task boundary), with CONC-008 ('go' callee must return void) moving
from planned to implemented.

Phase 11 added three codes, all in ADR 0010 D4: BORROW-021 (a plain pointer at a
'mut' parameter), BORROW-022 (a shared borrow at a 'mut' parameter) and
BORROW-023 (the lender passed beside its own mutable borrow). BORROW-002 and
BORROW-003 gained a call-site reading: two borrows of one lender passed to one
call are rejected when at least one is mutable.

Phase 10 added nine codes: ADT-005 and ADT-006 (constructor context), TYPE-012
(slice first argument shape), TYPE-013 (type-argument arity) and TYPE-014
(unclosed type-argument list) at increment 1; ADT-016 (a payload enum with
a fixed underlying type) and ADT-017 (a payload enum completing a forward
`enum` reference) at increment 2a; and ADT-018 (an owning payload enum across
a function boundary), ADT-019 (an owning payload that is not moved in or
adopted) and ADT-020 (an arm that would leave an owning payload bound nowhere)
at increment 2b.

A code is **implemented** when the frontend emits it and its test is in the dg
suite that the owning gate runs. Implemented today (Phases 4 to 16 of the roadmap): UAM-001, UAM-002, UAM-003, UAM-004, UAM-006, OWN-001, OWN-002, OWN-004,
OWN-007, OWN-009, OWN-010, OWN-011, OWN-012, OWN-013, BORROW-001 to BORROW-023,
DROP-001, DROP-002, DROP-003, DROP-006, DROP-007, DROP-008, TYPE-001, TYPE-002,
TYPE-004, TYPE-005, TYPE-006, TYPE-007, SAFE-001 to SAFE-005, FFI-001 to
FFI-011, plus Phase 10's ADT-001, ADT-002, ADT-003, ADT-004, ADT-005,
ADT-006, ADT-010, ADT-011, ADT-012, ADT-013, ADT-015, ADT-016, ADT-017,
ADT-018, ADT-019, ADT-020, TYPE-012, TYPE-013, TYPE-014, CONC-001 to
CONC-008 and CONC-010 to CONC-023. ADT-014 (match
used as an expression) is still planned: it is reachable only after the
expression form of `match` gets its own recovery path. Everything else is
planned; the KERNEL area is still reserved only,
because its owning ADRs are unwritten.
When the frontend lands, a code moves to `implemented` only when its test is in
`gcc/testsuite/crug.dg/diagnostics/` and the gate suite that owns it passes at
the same commit.
