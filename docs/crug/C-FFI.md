# C FFI contracts in the compiler

Crug calls unmodified C libraries through ordinary C declarations. A sidecar
file, `.crugffi`, adds the ownership, borrowing, destructor and thread
properties a prototype cannot state. The compiler checks each call against
it and lowers an owned return or out-parameter to the same drop code as
`adopt ()`; no metadata is loaded at run time, no ABI changes and no
wrapper library is generated.

Status: Phase 23 documentation of the Phase 9 and Phase 17 surfaces,
2026-10-20. Phase 9 (ADR 0006, ADR 0007) implements the contract; Phase 17
(ADR 0026) adds thread-affinity and Send/Sync enforcement. Gate evidence is
under `docs/crug/gates/phase-9/` and `docs/crug/gates/phase-17/`.

[FFI.md](FFI.md) is the full grammar and Phase 9 evidence,
[SEND-SYNC.md](SEND-SYNC.md) the crossing rules, and
[adr/0007-ffi-metadata-format.md](adr/0007-ffi-metadata-format.md) the format
decision. The compiler code is `gcc/c-family/crug-ffi.cc`; there is no
runtime module.

## 1. The contract format

A sidecar is one UTF-8 text file. Comments start with `#` and run to the end
of the line. The first non-comment, non-blank content must be the version
line `crugffi 1.0;`; a higher minor version is still grammar v1, and an
unknown declaration or clause is an error rather than an extension point. A
file larger than 1 MiB or holding more than 10,000 entries is rejected
(`crug-FFI-010`).

Two declaration forms exist:

~~~text
type opaque Node;
type struct Mutex sync !send;
fn make_node returns owned Node nullable drop free_node;
fn free_node consumes arg0 nullable;
fn fill_node out_owned arg0 drop free_node;
fn peek borrows arg0;
fn set_value mut_borrows arg0;
fn run_callback callback arg0;
fn live_count returns int;
~~~

Types are written without a star: `returns owned Node` names `Node *`, and
`type opaque Node` names `Node`. A destructor is an external `void D (T *)`.

The parser in `gcc/c-family/crug-ffi.cc` accepts these clause names:

| Clause spelling | Position | Meaning |
| --- | --- | --- |
| `returns owned <c-type>` | `fn` | the return is an owned pointer to the named type |
| `returns <c-type>` | `fn` | an unmanaged return, recorded only |
| `drop D` | `fn`, or after `returns owned` | destructor for the owned result |
| `consumes argN` | `fn` | the callee takes ownership of argument N |
| `borrows argN` | `fn` | the callee reads through argument N for the call |
| `mut_borrows argN` | `fn` | the callee may write through argument N for the call |
| `out_owned argN` | `fn` | the callee writes an owned pointer through argument N |
| `callback argN` | `fn` | argument N is a function pointer called during the call |
| `retained argN until F` | `fn` | the callee keeps argument N until F; recorded only |
| `blocking argN` | `fn` | the call may block; recorded only |
| `thread_affinity NAME` | `fn` | the result belongs to the named thread |
| `nullable argN` | `fn` | argument N may be a null pointer |
| `type [struct\|union\|enum\|opaque] T` | `type` | a C type or an opaque type |
| `drop D`, `opaque`, `send`, `!send`, `sync`, `!sync` | `type` | type properties |

`argref` is `argN` or `ret`; numbering starts at zero, and a variadic
parameter cannot be annotated. `nullable` and `drop` are modifiers on a
`returns` operand. ADR 0007 spells `blocking` with no operand, but the
current parser calls `parse_argref` after it, so the implemented spelling
is `blocking argN` (or `blocking ret`); that divergence is recorded in the
Phase 23 report, and a sidecar on this tree must use the parser spelling.

Clauses may not repeat for one operand; `drop` may appear once on the
`returns` operand and once at declaration level. Conflicting declarations,
within one file or between files, are `crug-FFI-004`; identical duplicates
merge silently. Merge order is command-line order, with no last-file-wins
rule; `type void` is `crug-FFI-002`.

## 2. Ownership at the boundary

The contract is checked at the call site, not inside the external library.
The checks are static and do not depend on a run-time registry.

| Clause | Accepted argument | Diagnostic on failure |
| --- | --- | --- |
| `consumes argN` | `move (e)` of an owned value, a fresh allocation, or null when `nullable` | `crug-FFI-007` |
| `out_owned argN` | `&x` where `x` is an owned variable in the EMPTY state | `crug-FFI-008` |
| `borrows argN` | a shared borrow, or an owned value not mutably borrowed | `crug-BORROW-002`, `-003` or `-016` |
| `mut_borrows argN` | a mutable borrow, or an owned value not borrowed at all | `crug-BORROW-016` |
| `callback argN` | a function declaration, function pointer, or null | `crug-FFI-009` |

An owned return binds an owned variable with the contract destructor, as
`adopt ()` does:

~~~c
own FILE *f = fopen ("/etc/hostname", "r");   /* dropped by fclose */
own void *p = malloc (16);                    /* dropped by free */
~~~

An `out_owned` argument initializes the owned object it points at:

~~~c
own Node *n;
fill_node (&n);        /* n is owned from this call on */
~~~

Destructor resolution is ordered: the `drop` on the `returns owned` clause,
then the `drop` of a `type` declaration for the type, then `crug-FFI-011`.
The destructor must be an external function taking exactly one pointer of
that type; there is no fallback to a similarly named function.

Drop lowering emits `if (p) dtor (p);` at the end of the owned variable's
scope, in the ordinary drop path (ADR 0005). It emits no helper call, no flag
and no `__crug_` symbol, so a program that uses only contracts and plain C
has no runtime dependency on libcrug. `scripts/check-ffi-codegen.sh` compares
the same plain C with and without a sidecar.

Under `-fcrug-strict`, passing an owned value to an unannotated function is
`crug-OWN-012`; a matching contract makes the call legal. A contract does
not change the C ABI: size, alignment, calling convention and symbol are the
header's (ADR 0006).

## 3. Thread affinity, Send and Sync

`thread_affinity NAME` states that the function's result belongs to the
named thread. The property follows the value through `move`, `borrow`,
`borrow_mut` and `&`. Capturing it in a task or sending it on a channel is
`crug-CONC-025`:

~~~text
error: a thread-affine value cannot cross to another task;
       it belongs to the 'render' thread [crug-CONC-025]
~~~

The only v1 escape is to keep the value on its thread. A transfer or
exclusion API is future work and is not implemented.

A `type` declaration can also state `send`, `!send`, `sync` or `!sync`.
These widen or restrict the structural inference in
[adr/0026-send-sync.md](adr/0026-send-sync.md) and [SEND-SYNC.md](SEND-SYNC.md):

| Crossing | Requirement | Diagnostic |
| --- | --- | --- |
| `go f (v)` | `v` is Send; an owned value is moved | `crug-CONC-024` |
| `go f (borrow (p))` in a `scope` | the pointee is Sync | `crug-CONC-026` |
| `go f (borrow_mut (p))` in a `scope` | the pointee is Send | `crug-CONC-024` |
| `send (c, v)` | the element type and `v` are Send | `crug-CONC-027`, `crug-CONC-024` |
| `chan T c = channel (n)` | `T` is Send | `crug-CONC-027` |

The checks are active when `-fcrug-goroutines` or `-fcrug-channels` is
enabled; there is no separate Send/Sync flag. A raw pointer is never Send by
inference, so `chan char *` is rejected until a sidecar annotates the
pointee. A wrong `send` is as serious as a wrong `consumes`: the compiler
cannot verify the callee's behaviour.

## 4. Discovery

`-fcrug-ffi=<file>` reads one explicit sidecar and may be repeated; the
command-line order is the merge order. An explicit file that cannot be read,
does not parse, or names an undeclared function is an error.

`-fcrug-ffi-discover` turns the layer on without naming a file. Discovery
applies only to quoted includes; for an `#include "contract.h"` it searches:

1. the directory of the file containing the `#include` line; 2. each
   directory in `CRUG_FFI_PATH`, in order; 3. `/usr/local/share/crug/ffi`;
   4. `/opt/crug/share/crug/ffi`.

Angle includes are never discovered, so `#include <stdio.h>` does not load a
system sidecar and a build stays reproducible. The search does not consult
`-I` directories: a header found through `-I` gets a sidecar only when it is
adjacent to the including file or named explicitly (ADR 0007 D5).

`-fcrug-strict` disables discovery and requires explicit files. A discovered
entry naming an undeclared function is ignored, because the header may not be
the one the sidecar was written for; an explicitly named entry in the same
position is `crug-FFI-006`. Both flags imply `-fcrug-ownership` and define
`__CRUG_FFI_VERSION__` as `major * 100 + minor` (`100` for v1.0).

## 5. Diagnostics

All eleven contract codes are errors in hybrid and strict modes; they are
registered in [DIAGNOSTICS.md](DIAGNOSTICS.md). A text diagnostic ends with
the bracketed code `[crug-FFI-NNN]`.

| Code | Meaning |
| --- | --- |
| `crug-FFI-001` | the sidecar does not parse: header, version, semicolon, character or keyword |
| `crug-FFI-002` | `type void` is not a valid declaration |
| `crug-FFI-003` | unknown or repeated clause, malformed `argN` or `ret` |
| `crug-FFI-004` | conflicting declarations, in one file or between files |
| `crug-FFI-005` | argument index out of range |
| `crug-FFI-006` | declaration does not fit the C prototype, or the name is not declared |
| `crug-FFI-007` | a `consumes` argument is not moved |
| `crug-FFI-008` | an `out_owned` argument is not the address of an empty owned variable |
| `crug-FFI-009` | a `callback` argument is not a function or null |
| `crug-FFI-010` | the file exceeds 1 MiB or 10,000 entries |
| `crug-FFI-011` | the destructor cannot be resolved |

Conflict reporting is deterministic: one command line produces byte-identical
stderr. `scripts/check-ffi-conflict.sh` asserts that for `crug-FFI-004`, and
`scripts/check-ffi-policy.sh` makes each of the eleven codes fire from a
fixture. There is no warning class for a contract.

## 6. Limits

- No runtime metadata: a sidecar is never loaded, looked up or copied at run
  time, and is not a dependency of the produced binary.
- No ABI change: a contract cannot change the size, alignment, calling
  convention or symbol of a declaration.
- No binding generator, no struct members, no macros and no variadic tails
  in metadata (ADR 0007 D9).
- No returned borrows, no conditional ownership and no refcounted APIs.
  `realloc` and `sqlite3_open` remain Level 1 `adopt` cases.
- `retained` and `blocking` are parsed and recorded but not enforced. The
  parser currently requires an operand after `blocking`, unlike the bare
  spelling in ADR 0007.
- No pointer structure in the file: a star in a sidecar is a syntax error,
  because the level comes from the C declaration.
- No `-I` search, no angle-include discovery, no signature hashing or version negotiation.
- No kernel metadata: plain GNU C under `-fcrug-kernel` (ADR 0017, Proposed), no catalogue.
- No callback lifetime analysis and no thread transfer API; both are future
  work, not implemented in v1.

A sidecar is only as good as its review: the compiler checks the caller's
obligations, not the library's behaviour.

## 7. Tests

The contract fixtures live in `gcc/testsuite/crug.dg/ffi/phase9/`: a
plain C library (`contracts_impl.c`) declared by `contracts.h` and
annotated by `contracts.crugffi`, plus 19 cases. The suite is script-driven
because every case links a shared fixture and passes `-fcrug-ffi=`.
`make check-FFI` runs it.

The cases cover accepted contracts (owned return, consume with move,
out-owned, borrows, mut-borrows, callback, nullable), rejected contracts
(consume without move, busy out-owned, shared-to-mut, callback not a
function), metadata failures (conflict, missing or bad version, unknown
clause, bad argument, bad destructor, unmatched declaration, `type void`),
discovery (an adjacent `contract.h` and `contract.crugffi`) and codegen
(`clean/malloc-free.c`, `clean/fopen-close.c`).

| Check | Expected result |
| --- | --- |
| `make check-FFI` | exit 0; the suite reports `ffi-suite: 19 cases, 0 failures` |
| `scripts/check-ffi-integration.sh` | `ffi-integration: 4 libraries unchanged, 4 programs OK` |
| `scripts/check-ffi-conflict.sh` | `ffi-conflict: crug-FFI-004 stable across 2 runs` |
| `scripts/check-ffi-codegen.sh` | `ffi-codegen: 2 files, 0 differences` |
| `scripts/check-ffi-policy.sh` | all `FFI-001` to `FFI-011` fire |
| `scripts/check-diagnostics.sh` | all FFI codes are registered and referenced |

The integration programs use four real libraries: libc (`fopen`/
`fclose`), SQLite (`sqlite3_open_v2`/`sqlite3_close` through wrappers),
libcurl (`curl_easy_init`/`curl_easy_cleanup`) and OpenSSL
(`SSL_CTX_new`/`SSL_CTX_free`). Each program counts opens and closes and
fails unless both are one. The check also hashes the four shared libraries
before and after linking, so a metadata-only change cannot silently rewrite
the library. The Phase 9 gate and its archived output are in
[EXIT-GATES.md](EXIT-GATES.md) phase 9.
