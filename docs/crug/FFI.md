# C FFI contracts (.crugffi)

Phase 9 implements the production C FFI layer of ADR 0006 and ADR 0007: a
sidecar file states, in text, which C functions transfer ownership, which
borrow, and which write owned values through out-parameters. The compiler reads
the sidecar, checks every call against the contract it declares, and attaches
the destructor that the contract names. No ABI changes, no runtime, no
metadata in the object file.

Status: Phase 9 implemented on 2026-10-19 against ADR 0007 (Accepted
2026-10-19). The exit gate and its archived evidence are in
EXIT-GATES.md phase 9 and docs/crug/gates/phase-9/.

## 1. What a contract says

A sidecar is one UTF-8 text file per library. Comments start with `#` and run
to the end of the line. The first content line must be:

```
crugffi 1.0;
```

The major version must be 1. A higher minor version is still grammar v1; an
unknown declaration or clause is an error, never ignored (ADR 0007 D6).

Two kinds of declaration exist. A type declaration records a property of a C
type; a function declaration records a contract for one external function.

```
type opaque sqlite3;
type shared pthread_mutex_t;
fn my_alloc returns owned Node nullable drop my_free;
fn my_free consumes arg0 nullable;
fn fill_node out_owned arg0 drop my_free;
fn peek borrows arg0;
fn set_value mut_borrows arg0;
fn run_cb callback arg0;
fn live_count returns int;
```

Types are written without pointers: `returns owned Node` names the pointer
type `Node *`, and `type opaque sqlite3` names `sqlite3`. A destructor is
the name of an external function that takes one pointer argument.

### 1.1 Clauses

| Clause | Written after | Meaning |
| --- | --- | --- |
| `returns owned T` | `fn` | the return value is an owned pointer to `T`; the caller must destroy it |
| `returns int` / `returns void` | `fn` | an unmanaged return; recorded for documentation only |
| `drop D` | `fn` or `returns owned` | the destructor for the owned return; the type declaration or `drop` must provide one |
| `consumes argN` | `fn` | the callee takes ownership of argument N; the caller must move a value in |
| `borrows argN` | `fn` | the callee only reads through argument N for the duration of the call |
| `mut_borrows argN` | `fn` | the callee may write through argument N for the duration of the call |
| `out_owned argN` | `fn` | the callee writes an owned pointer through argument N, a pointer-to-pointer, on every normal return |
| `nullable` | `fn` or `returns owned` | a pointer may be null; null owns nothing and is never destroyed |
| `callback argN` | `fn` | argument N is a function pointer the callee may call during the call |
| `retained argN until X` | `fn` | recorded only; no check is performed in v1 |
| `blocking`, `thread_affinity X` | `fn` | `blocking` is recorded only; `thread_affinity` is enforced since Phase 17 - the value belongs to that thread and cannot cross a task or channel boundary (`crug-CONC-025`, ADR 0026 D5) |
| `type opaque T` | `type` | the type may not be used by value |
| `type send T` / `type !send T` / `type sync T` / `type !sync T` | `type` | enforced at every task and channel crossing since Phase 17 (ADR 0026 D6, `SEND-SYNC.md`); the annotation is the author's safety claim |

Clauses may not repeat on one operand, except `drop` once at declaration
level and once on a `returns owned`. Conflicting clauses on one operand are
crug-FFI-004: `borrows` with `mut_borrows`, a borrow with a transfer,
`callback` with `out_owned`, `nullable` on a non-pointer.

### 1.2 Destructor resolution

A destructor is, in order: the `drop` clause on the declaration; the
`drop` clause of a `type` declaration for the same type; otherwise
crug-FFI-011. The destructor must be an external function declared with a
prototype that takes one pointer argument of exactly that type and is not
variadic. Its return type is not part of the contract: `fclose`, which returns
`int`, is a valid destructor.

## 2. Using a sidecar

```
crugc -fcrug-ffi=gcc/testsuite/crug.dg/ffi/libc.crugffi prog.c
crugc -fcrug-ffi=libc.crugffi -fcrug-ffi=openssl.crugffi prog.c
crugc -fcrug-ffi-discover prog.c
```

- `-fcrug-ffi=<file>` reads one explicit sidecar and may be repeated; the
  command-line order is the merge order. An explicit sidecar that cannot be
  read, does not parse, or names a function the translation unit does not
  declare is an error.
- `-fcrug-ffi-discover` turns the layer on without naming a file. Sidecars are
  then discovered for quoted includes: the directory of the file that contains
  the `#include` line, then the directories in `CRUG_FFI_PATH`, then
  `/usr/local/share/crug/ffi` and `/opt/crug/share/crug/ffi`. An entry
  discovered this way that names an undeclared function is ignored, because the
  header may simply not be the one the sidecar was written for.
- Angle includes are never discovered for: `#include <stdio.h>` does not load
  a system sidecar, so a build stays reproducible from the source tree.
- `-fcrug-strict` disables discovery; a strict build names its contracts
  explicitly.
- Both options imply `-fcrug-ownership` and define
  `__CRUG_FFI_VERSION__` as `MAJOR * 100 + MINOR` (v1.0 is `100`).

Explicit files and discovered files merge into one table. Two declarations that
differ in any clause are a conflict (crug-FFI-004), even when the second one is
the more specific. Identical duplicates merge silently. Nothing is
last-file-wins, because a silent override would make a build depend on argument
order.

## 3. What the compiler does with a contract

At a call site, for each argument with a clause:

| Clause | Check | When it fails |
| --- | --- | --- |
| `consumes argN` | the argument is `move (e)` of an owned value, a fresh allocation, or a null pointer if `nullable` | crug-FFI-007 |
| `out_owned argN` | the argument is `&x` where `x` is an owned variable in the EMPTY state | crug-FFI-008 |
| `borrows argN` | a shared borrow, or an owned value that is not mutably borrowed | crug-BORROW-002/003, crug-BORROW-016 |
| `mut_borrows argN` | a mutable borrow or an owned value not borrowed at all | crug-BORROW-016 |
| `callback argN` | a function declaration, a function pointer, or a null pointer | crug-FFI-009 |

An owned return binds an owned value with the contract destructor, exactly as
`adopt ()` does:

```
own FILE *f = fopen ("/etc/hostname", "r");   /* destroyed by fclose */
own void *p = malloc (16);                    /* destroyed by free */
```

An `out_owned` argument initializes the owned variable it points at, so the
variable is destroyed at its scope end with the contract destructor:

```
own Node *n;
fill_node (&n);        /* n is owned from here on */
```

The destructor runs through the same drop lowering as `adopt ()`: an
`if (p) dtor (p);` at the end of the variable's scope, with no helper call and
no flag. `objdump -d` of a program using contracts shows no `__crug_`
symbols.

An FFI contract also satisfies the strict-mode call policy. Without a contract,
passing an owned value to a `ref`/`mut` parameter is legal, but passing one
to an unannotated function is crug-OWN-012 under `-fcrug-strict`. With a
contract, the clause is the record that makes the call legal.

## 4. Diagnostics

| Code | Meaning |
| --- | --- |
| crug-FFI-001 | the sidecar does not parse (header, version, `;`, unexpected character, unknown keyword) |
| crug-FFI-002 | `type void` is not a valid declaration |
| crug-FFI-003 | unknown or repeated clause, malformed `argN`/`ret` |
| crug-FFI-004 | conflicting declarations, within one file or between files |
| crug-FFI-005 | argument index out of range |
| crug-FFI-006 | the declaration does not fit the C prototype, or a name is not declared in the translation unit |
| crug-FFI-007 | a `consumes` argument is not moved |
| crug-FFI-008 | an `out_owned` argument is not the address of an empty owned variable |
| crug-FFI-009 | a `callback` argument is not a function or null |
| crug-FFI-010 | the file exceeds 1 MiB or 10,000 entries |
| crug-FFI-011 | the destructor cannot be resolved |

All eleven are errors in hybrid and in strict mode, and all eleven are
registered in DIAGNOSTICS.md. Conflict reporting is deterministic: the same
command line produces byte-identical stderr, which is what
`scripts/check-ffi-conflict.sh` asserts.

## 5. What Phase 9 does not do

- No runtime metadata: nothing is loaded, looked up or copied at run time, and a
  sidecar is never a build or run dependency of the produced binary.
- No ABI change: a sidecar must not alter the size, alignment, calling
  convention or symbol of anything. `scripts/check-ffi-codegen.sh` compiles
  the same plain C with and without a sidecar and compares objects.
- No binding generator, no struct members, no macros, no variadic tails
  (ADR 0007 D9).
- No returned borrows, no conditional ownership (`realloc` and
  `sqlite3_open` stay Level 1 `adopt`), no refcounted APIs.
- `retained` and `blocking` are parsed and recorded but not enforced;
  `thread_affinity` and the `send`/`sync` annotations are enforced since
  Phase 17 at every task and channel crossing (`SEND-SYNC.md`).
- Discovery does not search `-I` directories: a header found through
  `-I` gets its sidecar only when the sidecar sits next to the including
  file, or when it is named explicitly. This is recorded as an ADR 0007 D5
  limitation, not a bug.

## 6. Tests

The contract fixtures live in `gcc/testsuite/crug.dg/ffi/phase9/`: a plain C
library (`contracts_impl.c`) declared by `contracts.h` and annotated by
`contracts.crugffi`, plus 19 cases. The suite is script-driven rather than
DejaGnu-driven because each case links a shared fixture library and passes
`-fcrug-ffi=`; `make check-FFI` runs it.

| Area | Files |
| --- | --- |
| accepted contracts | run-returns-owned, run-consume-move, run-out-owned, run-borrows, run-mut-borrows, run-callback, run-nullable |
| rejected contracts | reject-consume, reject-out-owned-busy, reject-shared-to-mut, reject-callback |
| metadata | conflict-a/b, bad/no-version, bad/version-2, bad/unknown-clause, bad/bad-arg, bad/bad-dtor, bad/unmatched, bad/type-void |
| discovery | adjacent/ (contract.h, contract.crugffi, use.c) |
| codegen | clean/malloc-free.c, clean/fopen-close.c |

Integration programs live in `gcc/testsuite/crug.dg/ffi/integration/` and use
four real libraries: libc (`fopen`/`fclose`), SQLite
(`sqlite3_open_v2`/`sqlite3_close` through wrappers), libcurl
(`curl_easy_init`/`curl_easy_cleanup`) and OpenSSL
(`SSL_CTX_new`/`SSL_CTX_free`). Each program counts its own opens and
closes and fails unless the count is one and one; the check also hashes the
four shared libraries before and after to show that linking them with metadata
changed nothing on disk.

| Script | Asserts |
| --- | --- |
| `scripts/check-ffi-suite.sh` | 19 cases, 0 failures |
| `scripts/check-ffi-integration.sh` | 4 libraries unchanged, 4 programs OK, each handle closed exactly once |
| `scripts/check-ffi-conflict.sh` | crug-FFI-004 stable across 2 runs, byte-identical stderr |
| `scripts/check-ffi-codegen.sh` | 2 files, 0 differences with and without the sidecar |
| `scripts/check-ffi-policy.sh` | FFI-001 to FFI-011 each fire from a fixture |

## 7. Exit gate

Phase 9 closes when `scripts/gate.sh --run phase-9` records:

1. `make check-FFI` exits 0 with `ffi-suite: 19 cases, 0 failures`.
2. `scripts/check-ffi-integration.sh` prints
   `ffi-integration: 4 libraries unchanged, 4 programs OK`.
3. `scripts/check-ffi-conflict.sh` prints
   `ffi-conflict: crug-FFI-004 stable across 2 runs`.
4. `scripts/check-ffi-codegen.sh` prints `ffi-codegen: 2 files, 0 differences`.
5. `scripts/check-ffi-policy.sh` prints `ffi-policy:` and all eleven codes.
6. `scripts/check-diagnostics.sh` exits 0.

Amendment A-10 of EXIT-GATES.md records why the original checks 2 and 3 were
replaced: they hashed a non-existent `lib/` directory and named an
`integration-sqlite` pseudo-command that no target implemented.

## 8. Deferred to later phases

| Item | Phase |
| --- | --- |
| place-level borrows and reborrowing | 12 |
| ownership across channel sends, task captures | 13 to 14 |
| struct members, macros, variadic tails in metadata | post-1.0 (ADR 0007 D9) |
| returned borrows, conditional ownership | after the first RFC revision of ADR 0007 |
| kernel-mode FFI (no metadata at all) | 19 |

## 9. Post-rc1: destructor and argN resolution (2026-10-26)

`crug-FFI-011` is no longer a loose "cannot be resolved" check. A destructor
named by `drop` must be an external function declared with a prototype,
non-variadic, with exactly one pointer parameter whose pointee matches the
owned value's type; a K&R definition, a variadic tail or a second parameter is
refused. The return type is not checked: a value-returning destructor such as
`fclose` is called as a statement and its result is discarded. The `argN`
clauses are independent: a clause is `arg` or `ret` followed by decimal digits,
with no sign and no trailing characters, and its index must fit `int`;
malformed text is `crug-FFI-003` and an out-of-range index stays `crug-FFI-005`.
New negative fixtures are `ffi/phase9/bad/dtor-{arity,knr,variadic}.c` and
`ffi/phase9/bad/argref-{malformed,range}.c`.
