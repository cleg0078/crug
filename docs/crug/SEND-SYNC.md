# Send and Sync: what may cross a worker boundary

Status: Phase 17 implemented 2026-10-20 (ADR 0026). The checks are compile
time only; the runtime never sees the property and the ABI does not change.

## 1. When the checks are active

There is no flag of their own. The two properties are inferred and enforced
when `-fcrug-goroutines` or `-fcrug-channels` is enabled, because only a task
or a channel can carry a value to another worker. A translation unit compiled
without either flag keeps plain C semantics, and `-fcrug-ownership` alone does
not turn the checks on.

Every crossing is checked by one module (`gcc/c/crug-send-sync.cc`):

| crossing | rule |
| --- | --- |
| `go f (v)` | the captured value must be `Send`; an owned value must be moved; an affine value is rejected |
| `go f (borrow (p))` in a `scope` | the pointee must be `Sync` |
| `go f (borrow_mut (p))` in a `scope` | the pointee must be `Send` |
| `send (c, v)` | the element type and the value must be `Send`; an owned value must be moved |
| `chan T c = channel (n)` | `T` must be `Send` |

## 2. Send

`Send` means "a copy of this value may be used by another worker".

| type | Send |
| --- | --- |
| integer, floating, complex, vector, boolean | yes |
| pointer to function | yes |
| `chan T` | yes (the handle is the shared object) |
| array of `Send` | yes |
| struct or union whose fields are all `Send` | yes |
| `own T *` with `T` `Send` | yes; `move ()` transfers it |
| `T *`, `raw T *`, pointer to `T` holding a pointer | no |
| `void *`, incomplete type | no |
| type annotated `send` in a `.crugffi` | yes (the sidecar author's claim) |
| type annotated `!send` in a `.crugffi` | no |

A raw pointer is never `Send` by inference. The documented escape is the FFI
annotation of the pointee type (section 6), so the claim is written down where
it can be reviewed.

## 3. Sync and scoped borrows

`Sync` means "a shared borrow of this value may be read concurrently". It has
the same base set as `Send`, with one caveat: a struct holding a pointer is
neither `Send` nor `Sync` unless the sidecar says otherwise.

A borrow captured by a task must live in a `scope` (ADR 0024). Inside one:

- `go f (borrow (p))` requires `T` in `own T *p` to be `Sync`
  (`crug-CONC-026`);
- `go f (borrow_mut (p))` requires `T` to be `Send` (`crug-CONC-024`);
- two children of one scope may not both reach the lender when one is mutable
  (BORROW-002 / BORROW-003, the scope rules of ADR 0024).

Outside a `scope` a borrow cannot be captured at all (`crug-CONC-012`), because
an unstructured task may outlive the borrow.

## 4. Channels

A channel carries values across tasks, so its element type must be `Send`
(`crug-CONC-027`) and every sent value must be `Send` (`crug-CONC-024`). An
owning element is transferred: write `send (c, move (value))` (`crug-CONC-003`),
the receiver owns the value, and an `adopt ()` destructor cannot cross
(`crug-CONC-023`). A `select` arm may not bind a receive from an owning-element
channel (`crug-CONC-017`) because the arm may not run.

~~~c
own char *buf = malloc (4);
chan own char *handoff = channel (1);

send (handoff, move (buf));	/* buf is moved; the reader owns it */
own char *got = recv (handoff);	/* got is owned here */
release (got);
~~~

## 5. Thread affinity

An FFI function may state the thread it belongs to:

~~~
crugffi 1.0;
fn render_slot returns int thread_affinity render;
~~~

A value initialized from `render_slot ()` belongs to the `render` thread. The
property follows the value through `move`, `borrow`, `borrow_mut` and `&`, and
capturing or sending it is `crug-CONC-025`:

~~~
error: a thread-affine value cannot cross to another task;
       it belongs to the 'render' thread [crug-CONC-025]
~~~

The escape is to keep the value on its thread. A transfer or exclusion API is
future work; the compiler does not accept a pointer annotation as a substitute.

## 6. FFI type annotations

The sidecar is the only place a program can widen the inference:

~~~
crugffi 1.0;

type struct Widget send sync;	/* the author claims it is safe to move and share */
type struct Plain !send;	/* the author forbids moving it to another worker */
~~~

A `send` annotation on a pointer type widens the pointee, so `own Widget *` is
accepted when `Widget` is annotated. A type may be declared once; two lines
that disagree are a sidecar error, not an override. A wrong `send` is a P0 bug
in the sidecar, like a wrong `consumes` or `returns owned` in ADR 0007.

## 7. Diagnostics

| code | message | when |
| --- | --- | --- |
| `crug-CONC-024` | a value of type '%s' cannot cross to another task; the type is not 'Send' | a capture or send of a non-`Send` value |
| `crug-CONC-025` | a thread-affine value cannot cross to another task; it belongs to the '%s' thread | a capture or send of an affine value |
| `crug-CONC-026` | a shared borrow of '%s' cannot cross to another task; the type is not 'Sync' | a shared borrow captured by a scoped task |
| `crug-CONC-027` | a channel element must be 'Send'; '%s' is not | `channel (n)` for a non-`Send` element |

The ownership codes keep their meaning at these crossings: `crug-CONC-003`
(send of an owned value without `move ()`), `crug-CONC-011` (an owned capture
without transfer), `crug-CONC-012` (a capture of a borrow outside a scope),
`crug-CONC-014` (an adopted destructor crossing a task) and `crug-CONC-023`
(an adopted destructor crossing a channel).

## 8. Limits

- The inference is structural. A pointer hidden in a `uintptr_t`, behind an
  opaque `void *`, or in an aggregate the ownership pass does not track is not
  seen; the FFI annotation is the documented escape for opaque types.
- An aggregate owning field is a known residual risk: the ownership pass does
  not track `struct { own char *p; }` (the Phase 5 MVP limit), so a struct with
  an `own` field can still be copied to another worker. Do not release the
  field in the receiver.
- There are no dynamic checks and no runtime lock injection; a value that is
  `Send` is still the programmer's responsibility to synchronise when it is
  shared.
- Kernel-mode concurrency is not modelled.

## 9. Tests

| check | result |
| --- | --- |
| `make check-SEND_SYNC` | `send-sync: 14 compiler test(s), 0 failure(s); raw pointer rejected, thread affinity enforced, FFI annotations enforced` |
| `make check-SEND_SYNC_DG` | the DejaGnu suite alone; 14 expected passes |
| `scripts/check-send-sync.sh` | the suite, the unchanged `spawn` runtime program, the raw-pointer rejection, both affinity cases and both FFI annotation cases |

The compiler cases live in `gcc/testsuite/crug.dg/send-sync/`: `send-int.c`,
`send-struct.c`, `send-own.c`, `send-chan.c`, `send-fnptr.c`, `sync-borrow.c`
(accepted forms) and `raw-ptr.c`, `struct-ptr.c`, `chan-ptr.c`, `sync-ptr.c`
(rejected forms). The runtime side is unchanged: the task suite delivers a
copied value (`libcrug/tests/goroutines/`), the channel suite transfers an owned
value (`libcrug/tests/channels/owned.c`), and an array capture is now rejected
rather than silently copied, so `go-capture.c` expects `crug-CONC-024`.
