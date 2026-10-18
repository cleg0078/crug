# The CRUG driver (phase 1)

**Status:** implemented for Phase 1 (plain C compatibility). Not a ratified
architecture decision; the ADR for driver architecture is due before Phase 3
adds the first Crug flag.

## What exists

`gcc/crugc` is an executable shell launcher. It resolves the GCC driver built
from this repository and executes it with the caller's arguments appended
unchanged:

1. `CRUGC_REAL_DRIVER`, when set. Tests use it to pin a driver.
2. `build/obj/gcc/xgcc`, for the out-of-tree build this repository uses.
3. `xgcc` next to the script, for an in-tree build.
4. `gcc` next to the script, for an installed tree.

For the first three it adds `-B<driver directory>/`, which is how an
uninstalled GCC finds `cc1`, the assembler and the target libraries. An
installed driver needs no such flag. If none is found the launcher exits 1; it
never falls back to the host `gcc`, because a comparison against the wrong
compiler would silently pass driver-drift tests.

## Why a launcher, and why it is allowed to exist

Phase 1 requires that `crugc` behave exactly like the GCC it is built from:
same default options, same include paths, same specs, same object code. The
cheapest way to guarantee that is to run the same binary. The launcher adds no
option parsing and no defaults; it only supplies the build-tree search path
that an installed driver would get from its prefix.

The gate makes the guarantee measurable rather than declared:

- `scripts/diff-cc.sh corpus/` compiles 30 programs with `gcc/crugc` and the
  baseline `xgcc` and compares exit status and object bytes.
- `scripts/check-no-hidden-state.sh` links a program and asserts that no
  `__crug_` symbol or `libcrug` dependency appears.
- `make check-C_COMPAT` runs the corpus at `-O0`, `-O2` and `-Os` and adds
  the two observable checks above.
- `scripts/gate.sh --run phase-1` runs the phase-1 CHECK lines verbatim and
  archives the result under `docs/crug/gates/phase-1/<commit>/`.

## What it does not do

It does not accept `-fcrug-*` flags, add include paths, link `libcrug`, or
change diagnostics: Phase 1 has no Crug syntax and no semantic flags. The
contextual words (for example `own`, `go`, `mut`) remain ordinary
identifiers, which `make check-C_COMPAT` also asserts.

## How it evolves

Phase 3 introduces the feature flags and the contextual-syntax infrastructure;
at that point the driver needs its own option handling and, before the ABI work
of Phase 4, its own specs. The launcher is then replaced by (or reduced to) a
driver built from the GCC driver sources with a Crug specs file, so that
`crugc` and `xgcc` share the option table but not necessarily the defaults.
The resolution rules above are the part worth keeping: the build tree must be
usable without installation, and a missing driver must be an error.

## Rules

- The launcher stays argument-transparent: no option is added, removed or
  reordered except the `-B` search path.
- A change that makes `crugc` differ from `xgcc` is a Phase 3+ change and
  needs a gate check plus an ADR if it is semantic.
- `gcc/crugc` is tracked and executable; generated build products stay in
  `build/`.
