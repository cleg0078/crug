# The 50 common diagnostic classes

This directory is the Phase 20 corpus: the fifty Crug diagnostics a new user is
most likely to run into, one minimal fixture per class, plus the compiler's own
diagnostics recorded as provisional golden text.

## Layout

- `top50.txt` — the published list: exactly 50 data lines and 50 distinct codes.
- `cases/<name>.c` — one minimal, self-contained fixture per class.  Plain
  C/Crug source with no testsuite directives; a one-line comment names the
  class.  A fixture may need an FFI sidecar, kept next to it as
  `cases/<name>.crugffi`.
- `golden/<code>.txt` — the compiler's stderr for that class, captured
  verbatim.  Goldens are generated, not hand-written, and are provisional: the
  Lead regenerates them once the origin-note column is filled.
- `README.md` — this file.

## top50.txt format

```
<code> | cases/<name>.c | <flags> | -
```

- `<code>` is a `crug-<AREA>-<NNN>` code registered in
  [docs/crug/DIAGNOSTICS.md](../../../../docs/crug/DIAGNOSTICS.md) — the
  registry, not this list, is the source of truth for codes.
- `<flags>` is only what the case needs to trigger the diagnostic.
  `-fsyntax-only` is added by whoever runs the case and is deliberately not
  listed.  Cases that need an FFI sidecar carry
  `-fcrug-ffi=cases/<name>.crugffi`.
- The last column is the origin note.  It is `-` for now; the Lead fills it in
  later, which is why goldens must be regenerated afterwards.

### Origin notes

Some diagnostics also emit a `note:` line that points at the origin of the
problem (for example `note: value was moved here`, or `note: the ownership
state here depends on which branch was taken`).  Those lines are part of the
golden text: a golden is the compiler's stderr verbatim, notes included.

At the moment `crug-UAM-001`, `crug-UAM-002`, `crug-DROP-006`,
`crug-DROP-007`, and `crug-CONC-011` carry an origin note; every other entry
has none and its top50 origin column stays `-`.  When the Lead fills that
column, regenerate the goldens — the notes are exactly what a filled column
should agree with.

## Regenerating a golden

From this directory:

```sh
PATH=/home/cleg/MyDev/crug/gcc:$PATH \
  /home/cleg/MyDev/crug/gcc/crugc -fsyntax-only -fno-diagnostics-color \
  <flags> cases/<name>.c 2> golden/<code>.txt
```

Every case is run with the fixture named as `cases/<name>.c` (not an absolute
path) so that file names in the diagnostics are stable.  A class may be an
error or a warning; `crug-OWN-007` is the only warning-only entry and exits 0.

To regenerate all of them:

```sh
while IFS='|' read -r code case flags rest; do
  code=$(echo "$code")
  case "$code" in ''|'#'*) continue ;; esac
  case=$(echo "$case" | tr -d ' ')
  flags=$(echo "$flags" | sed 's/^ *//;s/ *$//')
  PATH=/home/cleg/MyDev/crug/gcc:$PATH \
    /home/cleg/MyDev/crug/gcc/crugc -fsyntax-only -fno-diagnostics-color \
    $flags "$case" 2> "golden/$code.txt"
done < top50.txt
```

## Coverage

There are fifty entries, at least two from every area that has a code the
compiler can actually emit today:

| area | entries |
| --- | --- |
| UAM | 4 |
| OWN | 6 |
| BORROW | 10 |
| DROP | 5 |
| CONC | 6 |
| ADT | 6 |
| TYPE | 5 |
| SAFE | 5 |
| FFI | 3 |

`GRAMMAR` and `KERNEL` do not appear.  `GRAMMAR-001`..`GRAMMAR-006` are
registered but the compiler does not emit them yet, and `KERNEL` has no
allocated codes (its ADRs are still unwritten).  Adding either would mean
listing a diagnostic that cannot be triggered, so the corpus stays at 50
entries across the nine usable areas until those codes are implemented.

The selection favours ordinary user mistakes — use of a moved value, a double
move, an unadopted owning local, a borrow conflict, a jump into an owning
scope, a wrong channel or match form — over internal or exotic checks.

## Ratification

The list is ratified by the owner.  Do not renumber or rescope it: add a new
class by replacing an entry, keeping the count at 50, and regenerate the
affected goldens.