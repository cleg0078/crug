# CRUG Project Status

Last updated: 2026-10-20

## Where the project stands

The repository contains specification text, a phase-gate adjudicator, a
reproducible GCC build, the `crugc` driver (Phase 1), and from Phases 4 and 5 the
first Crug productions - `own`, `move`, automatic destruction, `adopt`,
`release` and `drop` - with their analysis, diagnostics and dg suites, from
Phase 6 the borrow checker (`ref`, `mut`, `borrow`, `borrow_mut`) and from
Phase 7 last-use lifetimes and the depth-zero interprocedural policy. Phase 13
adds `libcrug`'s platform abstraction layer (`libcrug/pal`, two backends, one
contract suite); Phase 14 adds the task runtime and the `go`/`yield` lowering
(M tasks on N workers, fixed stacks with a guard page, x86-64 System V context
switch); Phase 15 adds `scope`, the task group that owns its children and joins
them on every exit, and from Phase 16 channels (`chan T`, `channel`, `send`,
`recv`, `close`) and `select`, so the concurrency surface of the roadmap
exists, and from Phase 17 the static `Send`/`Sync` rules that decide what
may cross a worker or channel boundary at all. Phase 18 ports that runtime to
four hosts - Linux x86-64 and AArch64, Windows x86-64 and macOS arm64 - from
one core: one PAL backend per operating system, one context switch per ABI
(ADR 0012), a task pinned to the worker that first runs it, the PAL's page size
(v1.1) as the guard-page and stack-rounding unit, and one CI job per host, so the portability promise is measured instead of stated; the Phase 10 safe
types (`slice`, `option`, `result`, payload enums, owned payloads, `match`,
checked indexing) exist and are described below, Phase 11 lets the
optimizer use proven ownership (`-fcrug-alias-facts`, `mut`/`own` parameters
as `restrict`), and Phase 12 adds `c2crug`, a conservative C-to-Crug migration
tool (proof-based `own` insertion, `raw` for everything unproven). The `kernel-compat`
CI job builds the pinned baseline on every push and is a required check on `main`.
Four specification
documents exist (10,513 lines): the RFC, the master specification, the Rust memory
gaps analysis, and the production roadmap. An analysis of those documents produced
eight findings; this file tracks what has been resolved and what blocks progress.

The RFC has since been revised to 0.2-draft: its surface grammar is now normative (sections
13-25) and it adopts the ADR 0003/0004/0005 ownership and drop rules. Six decision records
(0001, 0002, 0003, 0004, 0005, 0018) and the precedence register were ratified on
2026-10-18, and the repository is now GPL-3.0-or-later. The master specification, roadmap
and gaps analysis still carry their original wording.

Reusable infrastructure now exists: `scripts/gate.sh` adjudicates every phase gate
and archives evidence, and the documentation set below gives the specification a
single authority order, a grammar, and decision records for the open questions.
The pinned GCC 16.2.0 tree is imported into git and tagged `releases/gcc-16.2`,
so the repository root is the GCC top level and the fork base is reproducible
from a clone rather than from a tarball on one machine. The C testsuite runs
through a root-free DejaGnu (`scripts/bootstrap-dejagnu.sh`), and its 224,481-pass
result is archived at `docs/crug/baseline/phase0/`; the phase-0 and phase-1 gates
pass with archived checks, and `docs/crug/EXIT-GATES.md` was ratified on 2026-10-19,
so the gate definitions are binding and the phase-to-milestone mapping (C-06) is
settled. `gcc/crugc` is a launcher around the built GCC driver
for now, so plain C compatibility is measured rather than assumed
(`make check-C_COMPAT`, `scripts/diff-cc.sh`). Phase 2 is closed:
`-fcrug-kernel` is accepted and ignored, the 21 roadmap work items run against a
named kernel-style corpus (`make check-KERNEL_COMPAT`), object scans show no
`__crug_` symbol and no `libcrug` dependency, and freestanding units carry no
hidden runtime (`scripts/check-kernel-freestanding.sh`). The CI half runs:
`.github/workflows/kernel-compat.yml` builds the pinned baseline on every push,
and branch protection on `main` requires the `kernel-compat` check, so Phase 2
and M2 closed on 2026-10-19 at commit `78c759b`. The repository is public since
2026-10-19, which is what makes free branch protection and the faster public
runner available.

Phase 3 is implemented: the ten `-fcrug-*` options are declared and closed over
their implications, the feature macros exist only in Crug mode, and the flag
machinery changes no parse and no object byte. `docs/crug/FLAGS.md` is the
digest; `scripts/check-crug-flags.sh` (149 work items) and
`scripts/check-gnu-c-compat.sh` (50 files) are the local checks. The
productions the flags will gate - `own`, `move`, `borrow`, `slice`, `chan`,
`match` - do not exist yet: that is Phase 4 onward, and the contextual words
still parse as ordinary C identifiers under every combination. The Phase 3 gate
passes its four checks at commit `78c759b` (evidence under
`docs/crug/gates/phase-3/78c759b/`), and its prerequisite - the `kernel-compat`
job being mandatory at HEAD - is met, so M3 closed on 2026-10-19.

Phase 4 implements the ownership MVP: `own` is a declaration specifier that is
erased from the type, `move` is an intrinsic identifier that lowers to a
register copy, and the analysis tracks UNINIT/OWNED/MOVED/MAYBE_MOVED at
variable granularity. It rejects use-after-move, double move, implicit copy of
an owned value and use of a possibly-moved value after a join
(`crug-UAM-001`..`crug-UAM-004`), and `-fcrug-dump=ownership` prints the join
state. The ABI obligation is measured, not asserted: `abi-ptr.c` keeps
`sizeof`/`_Alignof` identical and `move-zero-cost.c` disassembles to zero calls.
`docs/crug/OWNERSHIP.md` is the digest; `make check-OWNERSHIP` runs 17 expected
passes under `gcc/testsuite/crug.dg/ownership/`.

Phase 5 adds automatic destruction: destructors are analysis metadata (ADR 0005
A-2), they come from `adopt (p, d)` or from the allocation catalogue
(`malloc`/`calloc`/`realloc`/`strdup`/`aligned_alloc` are freed with `free` when
it is declared), and every owning object is destroyed exactly once on scope
exit, `return`, `break`, `continue` and `goto`-out, in reverse declaration order,
including owning parameters (ADR 0005 A-1). `goto`-into-scope, ambiguous joins,
conditional initialization, assignment over an owned value and non-owned `drop`
operands are rejected with stable codes; `setjmp` and never-adopted values warn.
`docs/crug/DROP.md` is the digest; `make check-DROP` runs 39 expected passes and
the six `scripts/check-drop-*.sh` checks assert exactly-once counts, sanitizer
cleanliness, absence of `__crug_` helper calls and the dump. The Phase 5 gate
passes 7/7 at `eeac7df` (logs under `docs/crug/gates/phase-5/eeac7df/`), and the
Phase 4 gate was re-adjudicated at the same commit with 7/7.

Phase 6 adds the lexical borrow checker: `ref` and `mut` are qualifiers on
pointer declarators, `borrow`/`borrow_mut` are intrinsic identifiers that lower
to plain pointers, and one shared with many `ref` reads or exactly one `mut` is
enforced for the lexical extent of the borrow. A borrow may name a whole owned
object or an existing shared borrow; members, array elements, dereferences and
plain pointers are refused (`crug-BORROW-009`), as are a mutable borrow of a
borrow or of a non-owned object (`crug-BORROW-010`) and a mutable borrow
initialized from a shared one (`crug-BORROW-012`). Moves, drops, assignments and
reads that conflict with a live borrow are rejected with `crug-BORROW-002`..`-005`,
`-008` and `-014`..`-016`; an inner borrow cannot escape its block
(`crug-BORROW-007`). `ref`/`mut` change no `sizeof`, `_Alignof` or object byte:
`scripts/check-borrow-cost.sh` compares a borrowed function with its
plain-pointer twin and `scripts/check-borrow-abi.sh` asserts the layout
identities. `docs/crug/BORROW.md` is the digest; `make check-BORROW` runs 62
expected passes under `gcc/testsuite/crug.dg/borrow/`. The Phase 6 gate passes
7/7 at `848b02e` (logs under `docs/crug/gates/phase-6/848b02e/`).

Phase 7 turns the lexical borrow checker into a liveness analysis (ADR 0020): a
borrow ends after the last statement that reads it, so the owner can then be
moved, dropped, released or mutably borrowed. The set of live borrows is
computed backward over the structured body (`in(S) = use(S) | (out(S) -
def(S))`), joins union, loops are conservative and a `goto` in the function
falls back to the lexical rule. The interprocedural policy is depth zero (ADR
0019): a borrow crosses a call only as an argument to a `ref`/`mut` parameter
(`crug-BORROW-020`), may not be returned (`crug-BORROW-019`) or stored in
memory or a static (`crug-BORROW-018`), and no function is summarized, so a call
can never extend a borrow. `make check-BORROW` runs 88 expected passes over 46
fixtures, six programs under `real-c/` are compiled and run, and
`-fcrug-dump=borrow` marks a last-use end with `(last-use)`.
`docs/crug/BORROW.md` is the digest, and the Phase 7 gate passes 7/7 at
`f6fbde4` (logs under `docs/crug/gates/phase-7/f6fbde4/`).

Phase 8 adds the escape hatches and the strict policy (ADR 0021). `raw` is a
pointer with no ownership and the plain pointer's representation; `unsafe { ... }`
is a scope that suppresses exactly the unanalysed operations and nothing else.
`-fcrug-strict` rejects, outside an `unsafe` block, a `raw` dereference
(`crug-SAFE-001`), an integer-pointer conversion (`-002`), an owned value cast to
an unmanaged pointer (`-003`), an `asm` statement (`-004`) and the
`setjmp`/`longjmp` family (`-005`); at a call it requires `move` for an `own`
parameter (`crug-OWN-011`) and `release` or an `unsafe` block for a callee with
no ownership contract (`crug-OWN-012`), and a discarded `adopt` is an error in
both modes (`crug-OWN-013`). Strict mode changes no object byte:
`scripts/check-strict-codegen.sh` compares four programs compiled with and
without the flag. `docs/crug/STRICT.md` is the digest and `make check-STRICT_UNSAFE`
runs 21 expected passes, and the Phase 8 gate passes 7/7 at `e3d8662` (logs
under `docs/crug/gates/phase-8/e3d8662/`; the definition is the seven-check list
in `docs/crug/EXIT-GATES.md`, amendment A-9).

Phase 9 adds the production C FFI layer (ADR 0007). `.crugffi` sidecars are
parsed by `gcc/c-family/crug-ffi.cc` and consumed by the ownership pass:
`-fcrug-ffi=<file>` names a contract explicitly (repeatable, merged in
command-line order), `-fcrug-ffi-discover` finds one next to a quoted include,
and both imply `-fcrug-ownership` and define `__CRUG_FFI_VERSION__` as 100. A
`returns owned` function hands the caller an owned value with the contract's
destructor, `consumes` requires `move` (`crug-FFI-007`), `out_owned` fills an
owned variable (`-008`), borrows are checked against the live borrow set, and a
callback argument must be a function (`-009`). Splitting a printf-like format
argument is deliberately absent: a variadic tail is not annotated. Metadata
never changes generated code: `scripts/check-ffi-codegen.sh` compares objects
with and without a sidecar and `scripts/check-ffi-integration.sh` hashes
`libc.so.6`, `libsqlite3.so.0`, `libcurl.so.4` and `libssl.so.3` before and
after building four programs that open and close a real handle. The suite is 19
cases (`make check-FFI`), `docs/crug/FFI.md` is the digest, and the Phase 9 gate
is the six-check list in `docs/crug/EXIT-GATES.md` (amendment A-10). The Phase 9
gate passes 6/6 at `e1052f9` (logs under `docs/crug/gates/phase-9/e1052f9/`).

Phase 10 adds the safe core types. `slice<T>`, `option<T>`, `result<T,E>` and
payload `enum`s are ordinary records built by the C front end
(`gcc/c/crug-safe.cc`), with `slice (ptr, len)`, `some`/`none`/`ok`/`err` and
variant constructors, a `match` statement over the three sum shapes and checked
`s[i]` indexing (`__builtin_trap` on a length violation,
`-fno-crug-bounds-checks` to remove it). A payload enum is a record storage type
registered under its `enum` tag (tag `unsigned int` at offset 0, payload union
after it), so a tag reference keeps working while the value keeps a variant tag.
No runtime, helper or new GIMPLE opcode is involved, the constructors lower to
plain stores, and the layouts (`slice` 16/8; `option<int>` 8/4;
`result<int,int>` 8/4; `enum Message { Text (char *), Image (char *), Quit }`
16/8) are frozen in `docs/crug/ABI.md` section 3 and asserted by
`gcc/testsuite/crug.dg/safe-types/layout.c` and `payload-enum-layout.c`.
`make check-SAFE_TYPES` runs 77 expected passes,
`scripts/check-safe-types.sh` requires zero failures and
`scripts/check-safe-bounds.sh` asserts the trap and its absence.

An owned payload (increment 2b) is tracked by the ownership analysis, with
ownership derived from the type because `own` is erased from the declaration:
`Text (adopt (p, release))`, `Text (move (q))` and fresh allocations transfer
the destructor into the value, the drop at every exit is
`if (tag == V) { if (u.Text) release (u.Text); }` so an inactive variant never
reaches the call (asserted by `scripts/check-owned-payloads.sh` and the
`-O0` assembly it inspects), copying is `crug-UAM-003`, an owned scrutinee must
be moved into `match` (`crug-ADT-015`), an arm covering an owning variant must
bind the payload (`crug-ADT-020`), and the value may not cross a function
boundary by value (`crug-ADT-018`). `docs/crug/SAFE-TYPES.md` is the digest.
The Phase 10 gate (amendment A-13) has no prose left and **passes 10/10 at
`43996fe`** (logs under `docs/crug/gates/phase-10/43996fe/`), so M10 is
awarded: `make check-SAFE_TYPES`, the tally, the bounds script, the two layout
compiles, the forward-reference rejection, the non-exhaustive `match`
rejection, `scripts/check-owned-payloads.sh`, `make check-C_COMPAT` and
`scripts/check-diagnostics.sh`.

Phase 11 makes proven ownership visible to the optimizer. `-fcrug-alias-facts`
(off by default; implies `-fcrug-borrow`) compiles `mut` and `own` pointer
parameters as `restrict` and rejects a call site that cannot prove
exclusivity: a plain pointer at a `mut` parameter (`crug-BORROW-021`), a
shared borrow there (`crug-BORROW-022`), two borrows of one lender in one call
(`crug-BORROW-002`/`-003`) and a lender passed beside its own mutable borrow
(`crug-BORROW-023`). `-fcrug-dump=alias` records every derived fact, and
`scripts/check-stop-conditions.sh` audits each one against the source
declaration. The evidence is the three-arm matrix in `bench/`: each kernel is
built as plain C, C + hand-written `restrict`, and Crug `mut`/`ref`, with
one shared plain-C `main`, so the Crug and restrict kernel objects are
byte-identical at `-O2` and `-O3` (8/8 kernels); timing is reported but the
gate rests on identity because this host's runs of identical code swing by more
than a factor of two. `bench/run.py --matrix alias --check`, `--matrix alias
--lto` and `--plain-c --check` are the runnable checks, and
`docs/crug/OPTIMIZATION.md` is the digest. The Phase 11 gate (amendment A-14)
passes 7/7 at `135dea9` (logs under `docs/crug/gates/phase-11/135dea9/`),
so M11 is awarded.

Phase 12 adds `c2crug`, the migration tool (`./c2crug`, Python 3 standard
library only, decision record ADR 0023, digest `docs/crug/C2CRUG.md`). Safe mode
inserts `own` before a pointer only when a local structural proof holds -
known allocating origin (`malloc`/`calloc`/`realloc`/`reallocarray`/`strdup`/
`strndup`/`aligned_alloc` or a `.crugffi` `returns owned` function), exactly one
standalone release at the declaration's brace depth, no early exit that skips it,
and no address-of, reassignment, arithmetic, return, copy, unknown escape or use
after release - and then rewrites that release to `drop`. Everything else stays
byte-identical and is reported as `raw` with the line that defeated the proof.
Line count never changes, so comments and structure survive. The three modes are
`--analyze` (report only), `--safe -o OUT` (copy, refuse an existing OUT) and
`--diff` (print only); `--report` writes `crug-c2crug-report-v1`, validated by an
independent checker, and borrow suggestions (`ref` 0.80, `mut` 0.60) are advice
that no mode applies. The corpus `migration/` is eight runnable programs with
expected output, a `Makefile` test target and a libc sidecar: 5 `own` applied,
5 `raw` preserved, 10 of 222 source lines changed (4.5 percent), comment lines
16 -> 16, and both the original and the migrated tree print
`migration: 8 programs pass`. `scripts/check-c2crug.sh` runs the whole flow,
`scripts/test-c2crug.py` its 32 unit checks, `scripts/c2crug-diff.sh` the churn
band, and `make check-C2CRUG` both. The corpus replaces `corpus/` because those
thirty programs are compile-only fragments with no test suite (amendment A-15).
The Phase 12 gate (amendment A-15) passes 7/7 at `3aa78e7` (logs under
`docs/crug/gates/phase-12/3aa78e7/`), so M12 is awarded.

Phase 13 creates `libcrug`, the runtime tree, with one rule: the core may reach
an operating system only through `libcrug/pal/include/crug_pal.h`. The header
froze PAL v1 (ADR 0011, `docs/crug/PAL.md`): thread create/join, CPU count,
wait/wake on a 32-bit atomic, memory reserve/commit/protect-guard/release, a
monotonic clock, sleep, a per-thread stable error slot and idempotent shutdown.
Two backends implement it - a fake one that runs a thread's entry inside `join`,
never blocks on a wait, backs memory with `malloc` and declines guard pages, and
a Linux one on `pthread`/`futex`/`mmap`/`clock_nanosleep`. The twelve-case
contract suite lives in `libcrug/core/pal_contract.c` (it includes only
`<crug_pal.h>` and language headers, and reports through a callback), so the
core is exercised against the fake PAL exactly as the gate phrase says.
Measured: `pal-contract[fake]: 12 cases, 0 failed, 1 skipped`,
`pal-contract[linux]: 12 cases, 0 failed, 0 skipped` plus
`guard-fault: SIGSEGV on the guarded page as claimed`,
`check-pal: fake and linux share 12 contract cases, 0 failures; fake backend
links no system primitive, guard pages fault on linux`, and
`lint-no-os-apis: 2 file(s) under libcrug/core, 0 operating-system primitive
references`. The lint matches names (including in comments) against the gate
list plus an extended list; architecture code stays out of the core, and the
layout follows the master specification's two axes (`libcrug/pal` for the OS,
`libcrug/arch` for the CPU) rather than the draft gate's `libcrug/pal/arch`.
The poller is deliberately not in v1: the phase's own non-goal defers
pollers and io_uring to post-1.0, and an interface with no client cannot be
validated (ADR 0011 D2). The Phase 13 gate (amendment A-16) runs seven
runnable checks and passes 7/7 at `e8a0b22` (logs under
`docs/crug/gates/phase-13/e8a0b22/`), so M13 is awarded.

Phase 14 implements the goroutine and task runtime MVP on top of that boundary.
On the compiler side, `go f (args);`, `go (call);` and `yield ()` are
statements gated by `-fcrug-goroutines`: the front end builds a per-call-site
frame struct with one field per captured argument, a static thunk that casts the
frame pointer back, and one `crug_task_spawn (thunk, &frame, sizeof frame)`
call, so the runtime never sees types, ownership or destructors
(`docs/crug/GOROUTINES.md`). The capture rules are the ownership contract at the
task boundary: an owned value needs `move` (CONC-011), a borrow cannot be
captured (CONC-012), a destructor cannot cross the boundary (CONC-014), and the
callee must be a non-variadic function with a prototype returning `void`
(CONC-008, CONC-010, CONC-013). On the runtime side, `libcrug/core/task.c` runs
M tasks over N workers (`min (processors, 8)`, `CRUG_TASKS_WORKERS`, capped at
64) on one FIFO ready queue guarded by an atomic spin lock, with cooperative
scheduling (return or `yield ()`), one `wait_u32` epoch for the workers, and
idempotent shutdown (ADR 0009). Each task's stack is a PAL reservation of
`CRUG_TASKS_STACK` bytes (default 64 KiB, rounded up to a PAL page) with one
guard page at the low end (`crug_pal_page_size ()`, 16 KiB on Apple silicon) and
a canary in the first committed word; it is never grown and never reused
while corrupted (ADR 0008). The context switch is 30 instructions under
`libcrug/arch/x86_64/sysv/`: it saves the six callee-saved integer registers,
the return address, and the MXCSR/x87 control words, matches `rsp % 16 == 8` at
entry, and treats XMM/YMM/ZMM as caller-saved (ADR 0013). Measured:
`make check-GOROUTINES` prints `goroutines: 28 compiler test(s), 5 task
program(s), 100000 tasks on 4 worker threads, 0 failure(s)`; the stress run
prints `tasks=100000 completed=100000 checksums=100000 lost=0 corrupt=0
workers=4 queue_peak=4` while `strace` counts 4 thread-creating calls;
`make -C libcrug check-sanitizer` is ASan/UBSan clean; the kernel corpus still
reports `kernel-objects: 20 objects, __crug_=0, libcrug=0`. The runtime ABI
version 1 freezes the spawn/yield signatures and the stats fields
(`docs/crug/ABI.md` section 6). The Phase 14 gate (amendment A-17) runs eight
runnable checks over these artefacts and passes 8/8 at `2ecfe35` (logs under
`docs/crug/gates/phase-14/2ecfe35/`).

Phase 15 makes concurrency structured. `scope { ... }` is a contextual statement
gated by `-fcrug-goroutines`: the parser gives the block an artificial `void *`
handle, stores `crug_scope_enter ()` in it as the block's first statement, and
every `go` inside the block lowers to `crug_task_spawn_scoped (handle, ...)`
instead of `crug_task_spawn (...)` (`docs/crug/SCOPE.md`). The wait is not
special-cased at the end of the block: the ownership pass models it as a
pseudo-destruction that is emitted before the block's own destructions, so
`return`, `break`, `continue` and `goto` out all wait first and a borrowed value
is still alive while the children run (ADR 0024 D3, D5). Two rules keep that
sound: a `goto` into a scope body is rejected (CONC-015) and a scoped task may
not borrow a value that dies before the block (CONC-016); inside the block a
`borrow`/`borrow_mut` capture is allowed, with the usual exclusivity rules, so a
mutable borrow taken by two tasks of one scope is BORROW-002/003. The runtime
side is a group object with an atomic pending count: a scoped spawn counts
before it queues, a completing task decrements and wakes, and
`crug_scope_leave` waits for zero - yielding inside a task, `wait_u32` on the
main thread - then frees the group. `crug_scope_stats_get` exposes
entered/left/live, so a leak is a number and not a hope. Measured: `make
check-SCOPE` prints `scope: 23 compiler test(s), 8 group program(s), 0 leaked
group(s), 0 failure(s)`; the eight programs print `scope-wait: children=8
slots=8`, `scope-owned: seen=S frees=1`, `scope-borrow: n=42`,
`scope-early: early=1 marks=2` and `scope-stress: tasks=1000 entered=200
left=200 leaked task group state: 0`; the sanitizer suite drives 3000 tasks
under 200 scopes with
LeakSanitizer clean. The group calls extend runtime ABI version 1 additively
(`docs/crug/ABI.md` section 6). The Phase 15 gate (amendment A-18) runs seven
runnable checks over these artefacts and passes 7/7 at `1f42cc7` (logs under
`docs/crug/gates/phase-15/1f42cc7/`), so M15 is awarded.

Phase 16 adds the way tasks pass values. `chan T` is one opaque pointer whatever
the element type is (frozen row in `docs/crug/ABI.md`), `channel (n)` takes its
type from the declaration initializer, a return statement or an array element,
capacity 0 is a handoff and a closed channel is observable at the receiver
(`docs/crug/CHANNELS.md`, ADR 0025). The ownership pass recognises the send as a
transfer: an owning element must be wrapped in `move ()` (CONC-003), `adopt`
across a channel is refused (CONC-023) and the receiver owns what it receives;
`select` cannot bind a receive from an owning-element channel (CONC-017),
because the arm may never run. The `select` lowering evaluates each arm's
channel once, polls the arms from a rotating start so no arm always wins, then
dispatches - a `default` is the final else, and with no ready arm the block
calls `crug_chan_poll_wait` and polls again. Measured: `make check-CHANNELS`
prints `channels: 40 compiler test(s), 7 channel program(s), 0 failure(s)`; the
seven programs cover a buffered round trip, a handoff, receive and send arms, an
owned transfer (`channels-owned: text=hi releases=1`), `close` and fan-in.
`chan.h` extends runtime ABI version 1 additively. The Phase 16 gate (amendment
A-19) passes 7/7 at `601e0cc` (logs under
`docs/crug/gates/phase-16/601e0cc/`), so M16 is awarded.

Phase 17 makes the boundary rules explicit: a value may cross to another task or
through a channel only when it is `Send` - scalars, function pointers, channel
handles, arrays and aggregates of `Send` values, and an own pointer whose
pointee is `Send` - and a shared borrow captured by a scoped task also requires
`Sync`; a raw or plain pointer is neither, so `go use (&x)`,
`go fill (buf, 4)` (array decay) and `chan char *` are rejected
(`CONC-024`, `CONC-027`). `docs/crug/SEND-SYNC.md` and ADR 0026 record the
rules; the checks need no flag of their own because they ride on
`-fcrug-goroutines`/`-fcrug-channels`, and only an FFI `.crugffi`
`send sync` annotation widens the inference. A thread-affine FFI result
(`thread_affinity`) cannot leave its thread (`CONC-025`), and a scoped mutable
capture must be `Send` while a shared one must be `Sync` (`CONC-026`).
Measured: `make check-SEND_SYNC` prints `send-sync: 14 compiler test(s), 0
failure(s); raw pointer rejected, thread affinity enforced, FFI annotations
enforced`. The Phase 17 gate (amendment A-20) runs seven runnable checks and
passes 7/7 at `42669c5` (logs under `docs/crug/gates/phase-17/42669c5/`),
so M17 is awarded.

Phase 19 qualifies the Linux kernel build. The pinned 6.18.57 tree
(`docs/crug/KERNEL-BASELINE.md`) is built out of tree with `CC=gcc/crugc`:
2,980 translation units, 0 errors, 0 warnings, `vmlinux` (51,420,536 bytes),
`arch/x86/boot/bzImage` and `System.map`. Seven stages make that measurable
(`scripts/kernel-stage.sh`): K1 compiles eight header probes as an external
module, K2 twelve translation units, K3 `lib/` as a whole (106 objects), K4 the
pinned 438-object corpus (`kernel/KERNEL-CORPUS.md`), K5 the full build, K6 boots
the result under QEMU 10.2.1 to a freestanding `/init` (`kernel/boot/init.c`)
that prints `crug-boot-ok` exactly once and powers off, and K7 compiles fifteen
pinned aarch64 objects with a cross compiler configured from this same tree
(`--target=aarch64-linux-gnu --without-headers`) and checks that they are
AArch64 relocatables. `scripts/kernel-object-diff.sh` rebuilds the corpus with
the wrapped GCC driver and compares object bytes: 437 of 438 are identical, and
the single difference is `init/version-timestamp.o`, whose `UTS_VERSION` embeds
the build time and build number; `kernel/object-diff-allow.txt` carries that
explanation, so an unexplained difference still fails. Kernel code remains plain
GNU C - no ownership lowering, no drop insertion, no `__crug_` symbol and no
libcrug dependency (ADR 0017, proposed) - and `scripts/check-p0-open.sh
--kernel` reads the new defect ledger `docs/crug/KNOWN-ISSUES.md` (no open P0).
The Phase 19 gate (amendment A-22) runs six runnable checks over these artefacts
and passes 6/6 at `3775c34` (logs under `docs/crug/gates/phase-19/3775c34/`),
so M19 is awarded. The closing push (`d54cda2`) is green: `kernel-compat`
run 23 and `multi-os` run 10 both succeeded.

Phase 20 turns the stable diagnostic codes into an interface. Every Crug
diagnostic is now emitted through one helper (`gcc/c-family/crug-diagnostics.cc`)
that attaches the code as a GCC diagnostic rule, so text output renders
`[crug-<AREA>-<NNN>]` from the rule rather than from a hand-written suffix, and
SARIF - the only machine-readable format GCC 16.2 implements - carries the code
in `ruleId` with a `helpUri` that resolves to `docs/crug/DIAGNOSTICS.md`
(`gcc/diagnostics/sarif-sink.cc` gained the metadata-rule branch). Fifty of the
129 registered codes have a fixture and a golden under
`gcc/testsuite/crug.dg/diagnostics/` (one case and one golden per code, listed
in `top50.txt`), and `scripts/check-diagnostic-goldens.sh` fails on any wording
drift, a duplicate code or a missing fixture; `scripts/check-diagnostic-sarif.sh`
asserts the `ruleId`, the rule entry and the origin note from the SARIF output.
Eight classes point at the construct that caused the error (UAM-001/002 "value
was moved here"; BORROW-002/003/016 "borrow created here"; CONC-011 and
DROP-006/007 already carried fix notes). The registry gained a user-facing
"Common errors and how to fix them" section with all fifty entries, the SARIF
consumption rule replaced the false `-fdiagnostics-format=json` claim, and
`docs/crug/TOOLING.md` plus `docs/crug/MIGRATION-EXAMPLES.md` document how
editors and analysers consume the compiler and what a migration looks like,
with executable examples checked by `make check-docs-examples`. ADR 0027
(tooling decisions) is written and Proposed. The Phase 20 gate (amendment A-23)
runs six runnable checks: the registry, the fifty-row corpus, the goldens, the
SARIF contract, the absence of GIMPLE jargon and the documentation examples.

The ABI question now has a policy: ADR 0006 (Accepted 2026-10-19) fixes two ABI classes,
erasure rules, boundary moves and versioning, and creates the normative registry
`docs/crug/ABI.md`. The registry records the C-ABI-transparent guarantees that
already hold and freezes a Crug-native layout when the phase that ships it lands:
Phase 10 froze `slice`, `option`, `result` (increment 1) and the payload enum
(increment 2a), and Phase 16 froze the `chan` row (one opaque pointer). ADR 0006
was ratified on 2026-10-19, so the policy is binding; only the Crug-native layout rows remain pending.

The FFI metadata question is now answered and binding: ADR 0007 (Accepted 2026-10-19) defines the
`.crugffi` v1 format - a versioned, line-oriented sidecar that states ownership,
borrowing, destructors, out-parameters, nullability, callbacks, blocking and
thread affinity for C functions, and nothing about layout. It cannot change
codegen, is never loaded at run time, rejects conflicts deterministically, and
under `-fcrug-strict` must be named explicitly rather than discovered. A reference
fixture, `gcc/testsuite/crug.dg/ffi/libc.crugffi`, implements the small libc
catalog of MASTER_SPEC section 16.5 minus `realloc`. Phase 9 implemented the parser, the discovery option and the call-site
consumption described in the paragraph above.

The cost of the next GCC bump is measured, not estimated:
`docs/crug/GCC-UPGRADE.md` records that the fork touches 9 upstream files with
32 hunks, that 31 of those hunks still apply with identical context to
upstream master (2026-10-19), that all 23 attachment symbols and all 8 behavioural
pins hold, and that a 31-check acceptance matrix
(`scripts/verify-upgrade.sh`) passes 31/31 at the current commit.
`scripts/fork-delta.sh` and `scripts/check-gcc-drift.sh` keep the measurement
reproducible. ADR 0022 (Proposed) turns it into policy: a re-pin and a minor
upgrade are maintainer-executable on the matrix, a major upgrade needs a
superseding ADR and owner ratification.

## Resolved in this revision

| Item | Artefact |
|------|----------|
| Documents had no order of authority | spec/000-PRECEDENCE.md (authority order + conflict register C-01..C-11) |
| Context-sensitive words had no disambiguation rule | docs/crug/adr/0002-contextual-keywords.md |
| Aggregate ownership was undefined (double-drop hole) | docs/crug/adr/0004-ownership-analysis-layer.md |
| Ownership storage could have required hidden state | docs/crug/adr/0003-ownership-metadata-storage.md (static metadata, ABI erasure) |
| Destruction across goto/setjmp/exit/tasks was undefined | docs/crug/adr/0005-drop-lowering.md |
| Syntax was sketched but ambiguous | docs/crug/adr/0018-grammar.md |
| RFC-0001 stated syntax without deciding ambiguity, attachment or ABI erasure | spec/CRUG_RFC_0001.md 0.2-draft sections 13-25 + Appendix H |
| ADR numbering conflicted with MASTER_SPEC §79 | resolved 2026-10-18: ADR 0003/0004 split + ADR 0005 renamed; mapping in docs/crug/adr/README.md |
| The decision set was unratified, so nothing could be cited as settled | ten ADRs carry Status Accepted (six on 2026-10-18; ADRs 0006 and 0007 on 2026-10-19; ADRs 0019 and 0020 on 2026-10-19); spec/000-PRECEDENCE.md Accepted (2026-10-18) |
| Diagnostic codes lived in message strings, so a code was unqueryable and a wording change could not fail a test | one helper (`gcc/c-family/crug-diagnostics.cc`) attaches the code as a diagnostic rule: SARIF `ruleId`, text suffix and `helpUri`; 50-class corpus with one golden per code under `gcc/testsuite/crug.dg/diagnostics/`; origin notes for eight classes; `make check-DIAGNOSTICS`, `make check-docs-examples`; ADR 0027 (Proposed) |
| No kernel source was pinned, so K0 and Phase 19 were untestable | docs/crug/KERNEL-BASELINE.md (Linux 6.18.57 LTS, tag/commit/SHA256, x86_64_defconfig, qemu markers) + scripts/kernel-pin.sh; C-09 resolved 2026-10-18 |
| Exit gates were partially unfalsifiable | docs/crug/EXIT-GATES.md |
| No stable diagnostic-code registry | docs/crug/DIAGNOSTICS.md (111 codes; 84 implemented through Phases 4 to 11) + scripts/check-diagnostics.sh |
| The kernel baseline had no CI protection, so Phase 2 could not close and Phase 4 could not start | `.github/workflows/kernel-compat.yml` green on `78c759b`; branch protection on `main` requires `kernel-compat`; phase-2 and phase-3 gates 4/4 archived under docs/crug/gates/phase-{2,3}/78c759b/ |
| No mechanism existed to evaluate a gate | scripts/gate.sh (`--list`, `--run`, archives per-check logs) |
| No upstream GCC was pinned | docs/crug/GCC-BASELINE.md + docs/crug/adr/0001-gcc-16.2-baseline.md + docs/crug/gcc-baseline.sha512 |
| The fork base lived outside git, so the tree could not be reproduced from a clone | GCC 16.2.0 imported as commit 57ccbae and tagged `releases/gcc-16.2`; `git diff releases/gcc-16.2..HEAD -- gcc/` is the fork delta |
| No way to reproduce the toolchain | Makefile (`make baseline`: verify, prerequisites, configure, all-gcc; `make build-full` adds target libraries and links; `make help`) |
| Repository licence was MIT, incompatible with the GCC fork | LICENSE (GPL-3.0-or-later), NOTICE (per-path map); C-07 resolved by the owner on 2026-10-18 |
| Line citations rotted as documents were revised | every citation now names a section or a gate phase and check (rule in spec/000-PRECEDENCE.md section 2) |
| The C testsuite could not run (no root, no DejaGnu) | `make bootstrap-dejagnu` installs DejaGnu/Expect/Tcl under `tools/`; `make check-c` runs the suite; result archived at docs/crug/baseline/phase0/ (224,481 pass, 84 environmental fail) |
| Phase 0 was unadjudicated | `scripts/gate.sh --run phase-0` passed 5/5 checks; logs under docs/crug/gates/phase-0/ |
| No driver existed, so plain-C compatibility could not be measured | `gcc/crugc` launcher (docs/crug/DRIVER.md) + `corpus/` (30 programs) + `scripts/diff-cc.sh` + `make check-C_COMPAT`; phase-1 gate archived under docs/crug/gates/phase-1/ |
| Exit gates were Proposed, so no gate definition was binding, and the phase-to-milestone mapping (C-06) was unsettled | docs/crug/EXIT-GATES.md Status Accepted (2026-10-19); C-06 settled by its section 7; amendment record in section 12 |
| There was no kernel-mode flag, corpus or local runner, so the kernel constraint was untested | `-fcrug-kernel` in `gcc/common.opt` (Common Ignore); 18-source corpus under `gcc/testsuite/crug.dg/kernel/`; `scripts/test-kernel-compat.sh` (21/21), `scripts/check-kernel-objects.sh`, `scripts/check-kernel-freestanding.sh`; `make check-KERNEL_COMPAT`; phase-2 gate archived for checks 1-3 |
| Nothing would have protected the kernel baseline in CI | `.github/workflows/kernel-compat.yml` (builds the pinned baseline, runs the suite, 300-minute cap, frees disk first) green on `78c759b` + `scripts/check-ci-gate.sh` (asserts the check concluded success and is a required status check) |
| No C ABI policy existed, so qualifier erasure, Crug-native type layouts and boundary moves were unsettled | docs/crug/adr/0006-c-abi-policy.md (two ABI classes, erasure, boundary moves, targets, versioning, test obligations) |
| No single place recorded ABI facts, while the Phase 10 gate already required one | docs/crug/ABI.md: C-ABI-transparent guarantees recorded, Crug-native rows pending, registry rules and layout-assertion obligation |
| The C FFI sidecar was illustrative only, so Level 2 ownership had no decidable format | docs/crug/adr/0007-ffi-metadata-format.md (v1 grammar, clause semantics, discovery, versioning, trust model) + reference fixture gcc/testsuite/crug.dg/ffi/libc.crugffi |
| C FFI contracts were a document, not a compiler feature | `gcc/c-family/crug-ffi.cc` (v1 parser, discovery callback, conflict detection), consumption in `gcc/c/crug-ownership.cc`, `-fcrug-ffi=` and `-fcrug-ffi-discover`; 19-case suite, four library integrations, five `scripts/check-ffi-*.sh`; docs/crug/FFI.md; phase-9 gate |
| The Phase 9 gate invoked the sidecar flag in a form the format does not define | docs/crug/EXIT-GATES.md phase 9 checks 4-5 now use `-fcrug-ffi=<path>` (amendment A-3) |
| No feature flag existed, so no later phase could be gated on one and no build could say which Crug features it wanted | ten `-fcrug-*` options in `gcc/common.opt` with closure in `gcc/c-family/crug-common.cc`; `docs/crug/FLAGS.md`; `scripts/check-crug-flags.sh` (149 work items), `scripts/check-gnu-c-compat.sh` (50 files), `scripts/diff-obj.sh`; fixtures in `gcc/testsuite/crug.dg/flags/` |
| Predefined feature macros contradicted the flags-off identity guarantee | ADR 0018 D9 R6 amendment A-1 and spec/000-PRECEDENCE.md C-12: `__CRUG__` and the feature macros exist only when a `-fcrug-*` option is active |
| Ownership had no implementation, so the state machine and the zero-metadata claim were untested | `own`/`move` front-end (gcc/c/crug-ownership.cc), 17-pass dg suite, `-fcrug-dump=ownership`; docs/crug/OWNERSHIP.md; phase-4 gate 7/7 |
| Destruction was undefined, so every owning object leaked and the O1 claim was untestable | drop lowering in `gcc/c/crug-ownership.cc`, `adopt`/`release`/`drop` intrinsics, 39-pass dg suite, six `scripts/check-drop-*.sh` checks, `-fcrug-dump=drop`; docs/crug/DROP.md; ADR 0005 A-1..A-7; phase-5 gate |
| Borrowing did not exist, so `ref`/`mut` were unimplemented words and the B1 class was unclaimed | `ref`/`mut` qualifiers and `borrow`/`borrow_mut` intrinsics in `gcc/c/crug-ownership.cc`; 62-pass dg suite; five `scripts/check-borrow-*.sh` checks; `-fcrug-dump=borrow`; docs/crug/BORROW.md; ADR 0018 A-4/A-5; phase-6 gate |
| Unanalysed operations (deref of a plain view, casts, asm, setjmp) had no policy, so "strict mode" was a word without a definition | ADR 0021 (Proposed; implemented and tested) + docs/crug/STRICT.md: `raw`, `unsafe` and five `crug-SAFE-*` restrictions with `-fcrug-strict`; C-08 resolved |
| A borrow lived for its whole block and could cross calls unchecked | last-use liveness in `gcc/c/crug-ownership.cc` (ADR 0020), depth-zero call policy (ADR 0019) with `crug-BORROW-018`..`-020`, 88-pass dg suite over 46 fixtures, six `real-c` programs, three new `scripts/check-borrow-*.sh` checks, `(last-use)` in the dump; docs/crug/BORROW.md; phase-7 gate |
| The safe core types did not exist, so "memory-safe Crug" had no data types and the Phase 10 layouts were still pending rows | `slice<T>`/`option<T>`/`result<T,E>`/payload enums/`match`/checked indexing in `gcc/c/crug-safe.cc`; 77-pass dg suite, `scripts/check-safe-types.sh`, `scripts/check-safe-bounds.sh` and `scripts/check-owned-payloads.sh`; `docs/crug/SAFE-TYPES.md`; frozen `ABI.md` rows; Phase 10 increments 1, 2a and 2b; phase-10 gate 10/10 |
| Ownership proved exclusivity but the optimizer never saw it, so `mut` was checked and then discarded | `-fcrug-alias-facts` (ADR 0010): `mut`/`own` parameters as `restrict`, BORROW-021..023 at unproven call sites, `-fcrug-dump=alias`, `bench/` three-arm matrix (Crug kernel objects byte-identical to the hand-written restrict twins, 8/8), `scripts/check-optimization.sh`, `scripts/check-stop-conditions.sh`; `docs/crug/OPTIMIZATION.md`; phase-11 gate 7/7 |
| Migrating C to Crug was unspecified, so "safe mode" had no proof rule and no corpus | `c2crug` (`--analyze`/`--safe`/`--diff`), ADR 0023, `docs/crug/C2CRUG.md`, `migration/` (8 programs with expected output), `docs/crug/c2crug-report.schema.json`, `scripts/validate-c2crug-report.py`, `scripts/c2crug-diff.sh`, `scripts/check-c2crug.sh`, `scripts/test-c2crug.py`; 5 own applied, 5 raw preserved, 4.5% churn, comments unchanged; phase-12 gate 7/7 |
| The runtime could reach the operating system directly, so every port would become a scheduler fork | `libcrug/pal/include/crug_pal.h` (PAL v1, ADR 0011), `libcrug/core/pal_contract.c` (twelve backend-agnostic cases), `libcrug/pal/fake` and `libcrug/pal/linux`, `scripts/check-pal.sh` (backend parity, no system symbol in the fake object), `scripts/lint-no-os-apis.sh`, `make check-PAL`, `docs/crug/PAL.md`; phase-13 gate A-16 |
| The language had `go`/`yield` in its grammar but no lowering and no runtime, so M:N concurrency was a word | `gcc/c/crug-tasks.cc` (frame + static thunk + `crug_task_spawn`), parser and capture hooks, CONC-008/010..014, `libcrug/core/task.c` (M:N, one ready queue, cooperative, epoch wait), `libcrug/arch/x86_64/sysv` (switch, stack preparation), 27 dg cases, five runtime programs, 100000-task stress run, `scripts/check-goroutines.sh`, `make check-GOROUTINES`; `docs/crug/TASKS.md`, `docs/crug/GOROUTINES.md`; ADRs 0008/0009/0013; phase-14 gate A-17 |
| A task could outlive every lexical block that explains it, so a borrowed capture was unsound and a child could still run after its data died | `scope { ... }` (task group) in `gcc/c/crug-tasks.cc`, `gcc/c/crug-ownership.cc` and `gcc/c/c-parser.cc`, CONC-015/016, scoped borrow captures with BORROW-002/003, runtime `crug_scope_enter`/`crug_scope_leave`/`crug_task_spawn_scoped`/`crug_scope_stats_get`, 23 dg cases, eight group programs, 3000-task/200-group sanitizer stress, `scripts/check-scope.sh`, `make check-SCOPE`; `docs/crug/SCOPE.md`; ADR 0024; phase-15 gate A-18 |
| A task could not pass a value to another task, so the concurrency surface stopped at spawn/yield/scope | `chan T`, `channel`, `send`, `recv`, `close` and `select` in `gcc/c/crug-chan.cc`, `gcc/c/c-parser.cc` and `libcrug/core/chan.c`; CONC-017..023; 40 dg cases and seven channel programs, `scripts/check-channels.sh`, `make check-CHANNELS`; frozen `chan` ABI row; `docs/crug/CHANNELS.md`; ADR 0025; phase-16 gate A-19 |
| A value could cross a task or channel boundary unchecked, so the ownership model appeared to bless data races | `gcc/c/crug-send-sync.cc` (`Send`/`Sync` inference, thread affinity, FFI widening), CONC-024..027, ten dg cases, `scripts/check-send-sync.sh`, `make check-SEND_SYNC`; `docs/crug/SEND-SYNC.md`; ADR 0026; phase-17 gate A-20 |
| The cost of moving to a newer GCC was unmeasured, so the pin could not be revised on evidence | docs/crug/GCC-UPGRADE.md (9 files/32 hunks versus 7 new sources/5,021 lines; 31/32 hunk contexts clean on master, 23/23 symbols, 8/8 pins; 31-check matrix) + scripts/fork-delta.sh, scripts/check-gcc-drift.sh, scripts/verify-upgrade.sh + ADR 0022 (Proposed) |
| The runtime was one operating system and one ABI, so the portability promise was untested and the first Windows user would have found a Linux scheduler | `libcrug/pal/windows` and `libcrug/pal/darwin`, `libcrug/arch/x86_64/windows` and `libcrug/arch/aarch64`, a host-detecting `libcrug/Makefile`, `make check-MULTI_OS`, `scripts/check-multi-os-ports.sh` (Linux AArch64 under qemu and Windows x86-64 under Wine, 2/4/8 workers each), `.github/workflows/multi-os.yml` (four jobs), `docs/crug/PORTABILITY.md` and ADR 0012; the 2+ worker crash the new ports exposed was fixed by pinning a task to its first worker (ADR 0009 amendment A-1), and the macOS port's 16 KiB pages required PAL v1.1's `crug_pal_page_size` (ADR 0011 A-1); `multi-os` runs are green (39355413321 at `f086068`; the first green run was 39354431681 at `b964c75`), four jobs each; phase-18 gate A-21 |
| The kernel was pinned but never compiled, so the kernel claim rested on microtests and `class K0` was untestable | `scripts/kernel-stage.sh` stages K1-K7 (8 header probes, 12 translation units, `lib/` whole, the 438-object pinned corpus, the full build, the QEMU boot, 15 aarch64 objects with a cross compiler from this tree), `kernel/KERNEL-CORPUS.md`, `kernel/boot/init.c`, `kernel/probes/`, `scripts/kernel-object-diff.sh` (437 identical, 1 explained) + `kernel/object-diff-allow.txt`, `scripts/check-p0-open.sh` + `docs/crug/KNOWN-ISSUES.md` (0 open P0), `make check-KERNEL`; ADR 0017 (Proposed); phase-19 gate A-22 |

## Still open

| ID | Item | Blocked on |
|----|------|------------|
| C-05 | P1 conformance refers to a release matrix that does not exist | RFC-0010 |
| -  | ADR 0017 (kernel ownership policy) is Proposed: kernel code has no ownership, drop, borrow or race analysis in v1, and enabling any of it needs a superseding ADR with kernel allocation/release and RCU/lock rules | owner ratification; a later kernel ownership phase |
| -  | The aarch64 half of the kernel evidence is compile-only: 15 pinned objects under `arm64_defconfig`, no aarch64 boot and no aarch64 object diff | a later phase with an aarch64 boot harness |
| -  | The diagnostic corpus covers 50 of the 129 registered codes; the rest are exercised by their phase dg suites but have no golden, and GRAMMAR/KERNEL classes emit no diagnostic yet and are excluded on purpose | a later diagnostics increment; the corpus list `gcc/testsuite/crug.dg/diagnostics/top50.txt` is the extension point |
| -  | Only eight diagnostic classes carry an origin note (UAM-001/002, BORROW-002/003/016, CONC-011, DROP-006/007); the rest point at the use, not at the construct that created the obligation | a later diagnostics increment; the corpus fourth column is where a new note is declared |
| -  | There is no JSON diagnostic format (GCC 16.2 has none) and no LSP server, no formatter and no shipped editor grammar: `docs/crug/TOOLING.md` fixes the consumption contract (SARIF, dumps, a synthesized compilation database) but the tools themselves are not part of the tree, and ADR 0027 is Proposed | owner ratification of ADR 0027; an editor-tooling phase if the contract proves insufficient |
| -  | `.crugffi` v1 deliberately excludes returned borrows, conditional ownership (`sqlite3_open`, `realloc`), refcounted APIs, struct members, macros, inline functions and variadic tails; such calls stay level 1 (`adopt` at the call site) | a later FFI revision (ADR 0007 D9) |
| -  | A sidecar for a header found through `-I` is discovered only when it sits next to the file containing the `#include`, because libcpp reports the include directive, not the includer directory | ADR 0007 amendment A-1; use `-fcrug-ffi=<path>` explicitly |
| -  | `slice`, `option`, `result`, payload enum and `chan` layouts are frozen (`ABI.md` section 3); no other Crug-native type may cross a C interface yet | a later phase that adds one |
| -  | Ownership transfer is checked only where a contract exists: a `.crugffi` sidecar makes a return value owned and a consuming parameter require `move`, but an unannotated C function still neither returns an owned value nor receives one, and an owned value passed to an `own` parameter is diagnosed only under `-fcrug-strict` (`crug-OWN-011`) | a later phase (call-site direction); recorded in docs/crug/DROP.md section 8 and docs/crug/FFI.md |
| -  | Borrow checking stops at whole objects: struct and array-element borrows, mutable reborrow, and interior mutability are not implemented. A plain pointer at a `ref` parameter is still accepted, and at a `mut` parameter it is accepted only without `-fcrug-alias-facts` (with the flag it is `crug-BORROW-021`) | a later phase (place-level borrows); the FFI half of the call-site direction was decided in Phase 9; recorded in docs/crug/BORROW.md sections 2.4 and 5 |
| -  | An owned payload is tracked only for local values and `match` bindings, so an owning payload enum stored in a struct, union or array, or held in an `option`/`result` payload, has no tracked destructor | a later ownership increment for aggregates |
| -  | A channel is a ring with a rotation counter rather than a fairness guarantee: `select` has no timeout, blocked tasks burn scheduler turns, `send` on a closed channel is fatal, and there is no `range`/`select` cancellation | ADR 0025 D5 and D6; a later phase or RFC |
| -  | The task runtime has no preemption, no cancellation, no growable stack, no I/O and no TSan support (the switch is invisible to ThreadSanitizer); a blocking call blocks a worker | ADRs 0008, 0009 and 0013 state the limits; preemption, place-level races and dynamic race detection are a later phase, the poller is post-1.0 |
| -  | A task frame is a plain C value: no generic task type, no closure over a local's address, and no task result; a task that must report a value uses a channel (`CHANNELS.md`) | a later phase (generic task types) |
| -  | A `scope` waits cooperatively and has no cancellation, no error aggregation and no detached child: a child that never returns hangs the scope, and a waiting scope burns scheduler turns because there is no parked WAITING state in v1 | ADR 0024 D3 and D7; cancellation and error propagation are a later RFC (CRUG-RFC-0003), a parked-waiter state is future work |
| -  | A scoped borrow is whole-object only and must name a value declared at the scope's own block depth or shallower; reborrowing a field, borrowing an element and borrowing through a call are refused or unchecked as in Phase 7 | ADR 0024 D4; place-level borrows are a later phase |
| -  | The `Send`/`Sync` inference is structural and only an FFI sidecar can widen it: aggregate ownership is untracked, so a struct with an `own` field still counts as `Send` and copying it can double-free; there are no dynamic checks and no memory-ordering model | ADR 0026 D6 and D8; an ownership increment for aggregates |
| -  | PAL v1 has no I/O poller: the poller is deferred by the phase-13 non-goal (RFC-0001 section 32) | ADR 0011 D2 and D9; a v1.1 amendment when an I/O phase needs the poller |
| -  | The v1 matrix is Linux (x86-64 and AArch64), Windows x86-64 and macOS arm64; BSD is a post-1.0 policy decision, and bare-metal and RISC-V are not in v1 | ADR 0012 D2; a later phase or RFC |
| -  | The Windows port does not swap the TEB stack bounds, and the runtime never grows a stack, so v1 is safe by construction; a growable stack must add the swap | ADR 0012 D9; a later stack-growth phase |
| -  | The Windows and Darwin backends have no native test host in this development tree: Windows is cross-built and run under Wine on Linux, macOS only through the `macos-arm64` CI job (green at `f086068`, run 39355413321) | owner adds the `multi-os` jobs to the required checks on `main` (branch protection currently requires only `kernel-compat`) |
| -  | A task is pinned to the worker that first ran it, so there is no work stealing: an idle worker cannot take work from a busy one | ADR 0009 amendment A-1; a scheduler design that re-establishes cached `_Thread_local` addresses |
| -  | The version macros `__CRUG_VERSION_*`, `__CRUG_RUNTIME_ABI__` and `__CRUG_FFI_VERSION__` asked for by RFC-0001 section 10 are not defined | ADR 0013 (versioning); the feature macros exist today |
| -  | The alias fact is local to a callee's parameters: an unannotated caller is not checked, and no fact is derived for constructs whose ownership is untracked (owning payload enums inside aggregates, owned payloads in `option`/`result`) | ADR 0010 D8; a later ownership increment for aggregates |
| -  | ADR 0022 (baseline upgrade policy) is Proposed, so the P/M/J classes, the acceptance rule and the ratification split are not yet binding | owner ratification |
| -  | The upgrade procedure has never been executed against a real new release; the first M- or J-class upgrade is the drill | first GCC release after 16.2 |

## Next actions, in dependency order

1. **Open Phase 21 (performance qualification)**: the Phase 20 gate
   (amendment A-23) runs six checks over the diagnostics corpus, the SARIF
   contract and the documentation examples. Phase 21's deliverable is measured
   performance against the C baseline: the `bench/` matrix currently measures
   the alias-facts row only, and the phase needs a pinned harness, a reporting
   format and a zero-cost verdict per construct.
2. **Ratify ADR 0017, ADR 0021, ADR 0022 and ADR 0019/0020** as the owner: the
   kernel ownership policy, the strict policy, the baseline upgrade policy and the
   two borrow policies are implemented and measured but still carry Status
   Proposed.
3. **Add the `multi-os` jobs to the required checks on `main`**: the first run
   is green (run 39355413321 at `f086068`, four jobs), but branch protection
   currently requires only `kernel-compat`, so a port can regress without
   blocking a merge. The change needs the repository owner's token.

## Working rules

- PRECEDENCE.md section 2 decides any document conflict.
- A semantic change needs an ADR plus tests in `gcc/testsuite/crug.dg/`.
- No phase gate is skipped; EXIT-GATES.md is the definition of "done", and a claim
  without an archived `scripts/gate.sh` run does not count.
