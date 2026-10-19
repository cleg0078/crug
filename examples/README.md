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

`crugc` finds the build tree automatically (`build/obj/gcc/xgcc`); `make
baseline` creates it.  The full flag set is in `docs/crug/FLAGS.md` and the
ownership rules in `docs/crug/OWNERSHIP.md` and `docs/crug/DROP.md`.

A rejected program is as much an example as an accepted one: the second
column of the table is the point of the phase.
