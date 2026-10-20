# Examples

Small programs to try the current compiler.  They are self-contained: none of
them needs the test harness, and each one names the command that builds it in
its first comment.

One command builds them all:

    make examples

It compiles every accepted program into `build/examples/` (the binary names are
the ones in the table below), links the runtime examples against the libcrug
Linux objects, and compiles the two rejected programs with `-fsyntax-only` to
check that they still fail with `[crug-UAM-001]` and `[crug-BORROW-002]`.  The
per-example commands in the table stay authoritative, because the flags are the
point of each one; `make examples` is how you get them all at once.

The source directory is selectable, so the same command builds your own
programs:

    make examples EXAMPLES=path/to/programs OUT=build/examples-mine

A file whose basename is one of the examples in the table keeps that example's
flags and binary name; every other `.c` file is compiled as a plain program
with no `-fcrug-*` flag and linked with its basename, so the directory has to
hold runnable programs (each one needs a `main`).  Known rejected programs are
checked only when they are present.

| file | what it shows | build and run |
| --- | --- | --- |
| `01-hello.c` | plain C through `crugc`, no flag | `./gcc/crugc -O2 examples/01-hello.c -o build/hello && ./build/hello` |
| `02-auto-drop.c` | `own` + `adopt`: the destructor runs at scope end | `./gcc/crugc -O2 -fcrug-ownership examples/02-auto-drop.c -o build/auto-drop && ./build/auto-drop` |
| `03-move.c` | `move` transfers ownership to a callee that adopts and destroys it | `./gcc/crugc -O2 -fcrug-ownership examples/03-move.c -o build/move && ./build/move` |
| `04-use-after-move.c` | a program the compiler rejects (`[crug-UAM-001]`) | `./gcc/crugc -O2 -fcrug-ownership -fsyntax-only examples/04-use-after-move.c` |
| `05-borrow.c` | `ref`/`mut`: a shared borrow read by a callee, then a mutable borrow alone | `./gcc/crugc -O2 -fcrug-borrow examples/05-borrow.c -o build/borrow && ./build/borrow` |
| `06-borrow-conflict.c` | a program the compiler rejects (`[crug-BORROW-002]`: mutable while shared) | `./gcc/crugc -O2 -fcrug-borrow -fsyntax-only examples/06-borrow-conflict.c` |
| `07-nll.c` | last-use lifetimes: shared then mutable borrow of the same object in one block, then a move | `./gcc/crugc -O2 -fcrug-borrow examples/07-nll.c -o build/nll && ./build/nll` |
| `08-strict-mode.c` | the same `raw` dereference accepted in hybrid mode and rejected under `-fcrug-strict` (`[crug-SAFE-001]`), plus the `unsafe` fix | `./gcc/crugc -O2 -fcrug-ownership examples/08-strict-mode.c -o build/strict-hybrid && ./build/strict-hybrid` |
| `09-unsafe-asm.c` | an `asm` statement confined to an `unsafe` block, which strict mode requires | `./gcc/crugc -O2 -fcrug-strict examples/09-unsafe-asm.c -o build/unsafe-asm && ./build/unsafe-asm` |
| `10-ffi.c` | a `.crugffi` contract: `fopen` returns an owned `FILE *` and `fclose` consumes it, so the compiler closes the file at scope end | `./gcc/crugc -O2 -fcrug-ffi=gcc/testsuite/crug.dg/ffi/libc.crugffi examples/10-ffi.c -o build/ffi && ./build/ffi` |
| `11-safe-types.c` | `slice<int>`, `option<int>` and `result<int,int>` crossing function calls, with `match` on both sums | `./gcc/crugc -O2 -fcrug-slices -fcrug-result examples/11-safe-types.c -o build/safe-types && ./build/safe-types` |
| `12-bounds.c` | checked indexing: an out-of-range `s[i]` traps (exit 132, SIGILL), while `-fno-crug-bounds-checks` removes the trap | `./gcc/crugc -O2 -fcrug-slices examples/12-bounds.c -o build/bounds && ./build/bounds; echo "exit=$?"` |
| `13-payload-enum.c` | a payload `enum` (`Text (char *)`, `Number (int)` and the unit variant `Quit`), construction from each variant and a `match` covering every one | `./gcc/crugc -O2 -fcrug-adt examples/13-payload-enum.c -o build/payload-enum && ./build/payload-enum` |
| `14-owned-payload.c` | an owned payload (`Text (own char *)`): ownership moves in through `adopt`, each value is moved into `match`, the arm binding releases the text once, and the variants that own nothing release nothing | `./gcc/crugc -O2 -fcrug-adt -fcrug-ownership examples/14-owned-payload.c -o build/owned-payload && ./build/owned-payload` |
| `15-alias-facts.c` | `-fcrug-alias-facts`: a `mut` parameter is compiled as `restrict` and the caller passes `borrow_mut`; the dump records the fact | `./gcc/crugc -O2 -fcrug-borrow -fcrug-alias-facts examples/15-alias-facts.c -o build/alias-facts && ./build/alias-facts` |
| `16-goroutines.c` | `go`/`yield`: eight by-value captures plus one indirect `go (fn (99))`, two tasks that take ownership of a heap buffer with `move` and release it themselves, then the runtime stats after shutdown | `make -C libcrug PAL=linux && ./gcc/crugc -O2 -fcrug-goroutines -fcrug-ownership -Ilibcrug/core examples/16-goroutines.c libcrug/build/linux/task.o libcrug/build/linux/pal_linux.o libcrug/build/linux/switch.o libcrug/build/linux/stack.o -pthread -o build/ex-goroutines && ./build/ex-goroutines` |
| `17-scope.c` | `scope` (a task group): four children, a nested scope with a mutable borrow of an owned local, an owned capture moved into a task; the block waits for every child and the group counters balance | `make -C libcrug PAL=linux && ./gcc/crugc -O2 -fcrug-goroutines -fcrug-ownership -fcrug-borrow -Ilibcrug/core examples/17-scope.c libcrug/build/linux/task.o libcrug/build/linux/pal_linux.o libcrug/build/linux/switch.o libcrug/build/linux/stack.o -pthread -o build/ex-scope && ./build/ex-scope` |
| `18-channels.c` | `chan T`/`send`/`recv`/`close` and `select`: a scope fills a channel with tasks, an owned `char *` is moved through a channel and released by the receiver, and `select` receives from whichever channel is ready | `make -C libcrug PAL=linux && ./gcc/crugc -O2 -fcrug-channels -fcrug-ownership -Ilibcrug/core examples/18-channels.c libcrug/build/linux/task.o libcrug/build/linux/pal_linux.o libcrug/build/linux/switch.o libcrug/build/linux/stack.o libcrug/build/linux/chan.o -pthread -o build/ex-channels && ./build/ex-channels` |
| `19-send-sync.c` | what may cross a task or channel boundary: a struct of plain values and a channel handle are `Send`, a `mut` parameter is an exclusive scoped borrow, an owned buffer moves into a task; the comment names the constructs the same rules reject (raw pointer, `chan int *`, thread-affine FFI handle) | `make -C libcrug PAL=linux && ./gcc/crugc -O2 -fcrug-goroutines -fcrug-ownership -fcrug-borrow -fcrug-channels -Ilibcrug/core examples/19-send-sync.c libcrug/build/linux/task.o libcrug/build/linux/pal_linux.o libcrug/build/linux/switch.o libcrug/build/linux/stack.o libcrug/build/linux/chan.o -pthread -o build/ex-send-sync && ./build/ex-send-sync` |
| `stress-test-mem.c` | a 1,000,000-iteration ownership stress run: seven allocation patterns per iteration, every object destroyed once (created = destroyed, live 0) | `./gcc/crugc -O2 -fcrug-borrow examples/stress-test-mem.c -o build/stress && ./build/stress` |

`crugc` finds the build tree automatically (`build/obj/gcc/xgcc`); `make
baseline` creates it.  The full flag set is in `docs/crug/FLAGS.md` and the
ownership rules in `docs/crug/OWNERSHIP.md` and `docs/crug/DROP.md`, the
borrow rules in `docs/crug/BORROW.md`, strict mode in `docs/crug/STRICT.md`, the FFI contracts in `docs/crug/FFI.md`, and the
safe core types, payload enums and owned payloads in `docs/crug/SAFE-TYPES.md`,
ownership-aware optimization in `docs/crug/OPTIMIZATION.md`, and the `go` and
`yield` statements in `docs/crug/GOROUTINES.md` with the runtime in
`docs/crug/TASKS.md`, the `scope` statement in `docs/crug/SCOPE.md`,
and channels and `select` in `docs/crug/CHANNELS.md`, and the `Send`/`Sync`
crossing rules in `docs/crug/SEND-SYNC.md`.

A rejected program is as much an example as an accepted one: the second
column of the table is the point of the phase.
