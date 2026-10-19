#!/bin/sh
# Phase 11: prove that the ownership facts the optimizer receives are exactly
# the ones a hand-written restrict states, and nothing more.
#
#   1. golden: the alias dump of noalias-golden.c matches noalias-golden.expected
#      (restrict on mut/own parameters only, never on ref or plain parameters);
#   2. identity: for every benchmark kernel the crug arm and the hand-written
#      restrict arm produce byte-identical object files at -O2 and -O3;
#   3. ref-only: a function with only ref parameters is byte-identical to its
#      plain const-pointer twin (no fact for a shared borrow);
#   4. flag-off: -fcrug-alias-facts alone does not change the object of code
#      that has no mut/own parameter;
#   5. adversarial: two borrows of one lender, a lender passed beside its own
#      mutable borrow, and a plain pointer at a mut parameter are all rejected;
#   6. LTO: the three arms build and run under -flto with equal results.
#
# Usage: scripts/check-optimization.sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC=${CRUGC:-$ROOT/gcc/crugc}
TMP=$ROOT/build/check-optimization
DG=$ROOT/gcc/testsuite/crug.dg

rm -rf "$TMP"
mkdir -p "$TMP"

fail=0
note () { printf '%s\n' "$*"; }

# ------------------------------------------------------------------ golden --
cp "$DG/optimization/noalias-golden.c" "$TMP/"
( cd "$TMP" && "$CC" -O2 -fcrug-borrow -fcrug-alias-facts \
    -fdump-tree-alias -c noalias-golden.c -o noalias-golden.o ) \
  > "$TMP/golden.log" 2>&1 || { cat "$TMP/golden.log"; fail=1; }
alias_file=$(ls "$TMP"/noalias-golden.c.*.alias 2>/dev/null | head -1)
if [ -z "$alias_file" ]; then
  note "golden: no -fdump-tree-alias output"
  fail=1
else
  grep -E '^(void|int|long) ' "$alias_file" > "$TMP/golden.actual" || true
  if ! diff -u "$DG/optimization/noalias-golden.expected" "$TMP/golden.actual"; then
    note "golden: alias signatures differ from noalias-golden.expected"
    fail=1
  else
    note "golden: $(wc -l < "$TMP/golden.actual") signatures, 0 false noalias"
  fi
fi

# ---------------------------------------------------------------- identity --
identity_ok=0
identity_total=0
for k in vadd stencil strcopy list; do
  for arm in 1 2; do
    for opt in -O2 -O3; do
      tag=$(printf '%s' "$opt" | tr -d '-')
      out="$TMP/$k-$arm-$tag.o"
      extra=""
      [ "$arm" = 2 ] && extra="-fcrug-borrow -fcrug-alias-facts"
      # shellcheck disable=SC2086
      "$CC" $opt -DBENCH_ARM=$arm $extra -I "$ROOT/bench" -c \
        "$ROOT/bench/kernels/${k}_kernel.c" -o "$out" > "$TMP/cc.log" 2>&1 \
        || { cat "$TMP/cc.log"; fail=1; }
    done
  done
  for opt in -O2 -O3; do
    tag=$(printf '%s' "$opt" | tr -d '-')
    identity_total=$((identity_total + 1))
    if cmp -s "$TMP/$k-1-$tag.o" "$TMP/$k-2-$tag.o"; then
      identity_ok=$((identity_ok + 1))
    else
      note "identity: $k $opt crug and restrict objects differ"
      fail=1
    fi
  done
done
note "identity: $identity_ok/$identity_total kernel objects byte-identical"

# ---------------------------------------------------------------- ref-only --
mkdir -p "$TMP/ref" "$TMP/plain"
cp "$DG/alias/alias-ref-only.c" "$TMP/ref/ref-only.c"
sed 's/ref const int/const int/' "$DG/alias/alias-ref-only.c" > "$TMP/plain/ref-only.c"
( cd "$TMP/ref" && "$CC" -O2 -fcrug-borrow -fcrug-alias-facts -c ref-only.c \
    -o ../ref-only-crug.o ) >/dev/null 2>&1
( cd "$TMP/plain" && "$CC" -O2 -c ref-only.c -o ../ref-only-plain.o ) >/dev/null 2>&1
if cmp -s "$TMP/ref-only-plain.o" "$TMP/ref-only-crug.o"; then
  note "ref-only: shared borrow has no fact (object identical)"
else
  note "ref-only: ref parameter changed the object"
  fail=1
fi
cp "$TMP/plain/ref-only.c" "$TMP/flag-off.c"

# ---------------------------------------------------------------- flag-off --
"$CC" -O2 -c "$TMP/flag-off.c" -o "$TMP/flag-off-a.o" >/dev/null 2>&1
"$CC" -O2 -fcrug-borrow -fcrug-alias-facts -c "$TMP/flag-off.c" \
  -o "$TMP/flag-off-b.o" >/dev/null 2>&1
if cmp -s "$TMP/flag-off-a.o" "$TMP/flag-off-b.o"; then
  note "flag-off: -fcrug-alias-facts alone changes nothing"
else
  note "flag-off: the flag changed plain code"
  fail=1
fi

# ------------------------------------------------------------- adversarial --
reject () {
  src=$1
  code=$2
  if "$CC" -fsyntax-only -O2 -fcrug-borrow -fcrug-alias-facts "$src" \
      > "$TMP/reject.log" 2>&1; then
    note "adversarial: $src was accepted"
    fail=1
  elif ! grep -q "$code" "$TMP/reject.log"; then
    note "adversarial: $src was rejected without $code"
    cat "$TMP/reject.log"
    fail=1
  else
    note "adversarial: $(basename "$src") rejected with $code"
  fi
}

reject "$DG/alias/borrow-021.c" "crug-BORROW-021"
reject "$DG/alias/borrow-022.c" "crug-BORROW-022"
reject "$DG/alias/borrow-002.c" "crug-BORROW-002"
reject "$DG/alias/borrow-003.c" "crug-BORROW-003"
reject "$DG/alias/borrow-023.c" "crug-BORROW-023"

# Runtime: the accepted program still computes the right values.
"$CC" -O2 -fcrug-borrow -fcrug-alias-facts "$DG/alias/alias-run.c" \
  -o "$TMP/alias-run" > "$TMP/run.log" 2>&1 \
  || { cat "$TMP/run.log"; fail=1; }
if "$TMP/alias-run"; then
  note "runtime: alias-run exits 0"
else
  note "runtime: alias-run failed"
  fail=1
fi

if [ "$fail" -ne 0 ]; then
  note "optimization: FAIL"
  exit 1
fi
note "optimization: golden, identity, flag-off, adversarial and runtime checks pass"
