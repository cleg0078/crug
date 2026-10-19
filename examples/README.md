# Examples

Small programs to try the current compiler.  They are self-contained: none of
them needs the test harness, and each one names the command that builds it in
its first comment.

| file | what it shows | build and run |
| --- | --- | --- |
| `01-hello.c` | plain C through `crugc`, no flag | `./gcc/crugc -O2 examples/01-hello.c -o build/hello && ./build/hello` |
| `02-auto-drop.c` | `own` + `adopt`: the destructor runs at scope end | `./gcc/crugc -O2 -fcrug-ownership examples/02-auto-drop.c -o build/auto-drop && ./build/auto-drop` |
| `03-move.c` | `move` transfers ownership to a callee that adopts and destroys it | `./gcc/crugc -O2 -fcrug-ownership examples/03-move.c -o build/move && ./build/move` |
| `04-use-after-move.c` | a program the compiler rejects (`[crug-UAM-001]`) | `./gcc/crugc -O2 -fcrug-ownership -fsyntax-only examples/04-use-after-move.c` |
| `05-borrow.c` | `ref`/`mut`: a shared borrow read by a callee, then a mutable borrow alone | `./gcc/crugc -O2 -fcrug-borrow examples/05-borrow.c -o build/borrow && ./build/borrow` |
| `06-borrow-conflict.c` | a program the compiler rejects (`[crug-BORROW-002]`: mutable while shared) | `./gcc/crugc -O2 -fcrug-borrow -fsyntax-only examples/06-borrow-conflict.c` |
| `07-nll.c` | last-use lifetimes: shared then mutable borrow of the same object in one block, then a move | `./gcc/crugc -O2 -fcrug-borrow examples/07-nll.c -o build/nll && ./build/nll` |
| `stress-test-mem.c` | a 1,000,000-iteration ownership stress run: seven allocation patterns per iteration, every object destroyed once (created = destroyed, live 0) | `./gcc/crugc -O2 -fcrug-borrow examples/stress-test-mem.c -o build/stress && ./build/stress` |

`crugc` finds the build tree automatically (`build/obj/gcc/xgcc`); `make
baseline` creates it.  The full flag set is in `docs/crug/FLAGS.md` and the
ownership rules in `docs/crug/OWNERSHIP.md` and `docs/crug/DROP.md`, and the
borrow rules in `docs/crug/BORROW.md`.

A rejected program is as much an example as an accepted one: the second
column of the table is the point of the phase.
