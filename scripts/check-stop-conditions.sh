#!/bin/sh
# Phase 11 stop conditions: a false noalias fact is a P0 and stops the phase.
#
# The audit is independent of the compiler's own bookkeeping: for every fact in
# every -fcrug-dump=alias file produced over the corpus, the source declaration
# of the named parameter must itself say 'mut' or 'own'.  A fact on a ref, a
# plain pointer or anything else would be a false noalias report and fails the
# check.  The script also requires that at least one fact was audited (a silent
# loss of all facts would otherwise pass) and that a ref-only function has no
# fact.
#
# Usage: scripts/check-stop-conditions.sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC=${CRUGC:-$ROOT/gcc/crugc}
TMP=$ROOT/build/check-stop-conditions

rm -rf "$TMP"
mkdir -p "$TMP"

# Sources compiled with the fact enabled.  The benchmark kernels exercise mut,
# ref and plain parameters; the fixtures add own and the negative cases.
CORPUS="$ROOT/bench/kernels/vadd_kernel.c
$ROOT/bench/kernels/stencil_kernel.c
$ROOT/bench/kernels/strcopy_kernel.c
$ROOT/bench/kernels/list_kernel.c
$ROOT/gcc/testsuite/crug.dg/alias/alias-run.c
$ROOT/gcc/testsuite/crug.dg/alias/alias-own.c
$ROOT/gcc/testsuite/crug.dg/alias/alias-ref-only.c
$ROOT/gcc/testsuite/crug.dg/optimization/noalias-golden.c"

facts=0
reports=0
for src in $CORPUS; do
  base=$(basename "$src" .c)
  obj=$TMP/$base.o
  dump=$TMP/$base.crug-alias
  case $src in
    */bench/kernels/*) arm=-DBENCH_ARM=2; extra="-I $ROOT/bench" ;;
    *) arm=""; extra="" ;;
  esac
  # shellcheck disable=SC2086
  "$CC" -O2 $arm $extra -fcrug-borrow -fcrug-alias-facts \
    -fcrug-dump=alias -c "$src" -o "$obj" > "$TMP/cc.log" 2>&1 \
    || { cat "$TMP/cc.log"; echo "stop-conditions: $src did not compile"; exit 1; }
  [ -f "$dump" ] || continue

  grep '^alias ' "$dump" > "$TMP/facts.txt" || true
  # Ref-only must produce no fact at all.
  case $base in
    alias-ref-only)
      if [ -s "$TMP/facts.txt" ]; then
        cat "$TMP/facts.txt"
        echo "stop-conditions: ref-only function received a fact"
        reports=$((reports + 1))
      fi
      continue
      ;;
  esac

  while read -r _ fn _ param _ kind; do
    [ -n "${param:-}" ] || continue
    facts=$((facts + 1))
    # The fact text is 'alias <fn>: parameter <name> restrict (mut|own)'.
    if ! grep -Eq "\b(mut|own)\b[^;{]*\b$param\b" "$src"; then
      echo "FALSE NOALIAS: $src: $fn parameter $param"
      reports=$((reports + 1))
    fi
  done < "$TMP/facts.txt"
done

if [ "$facts" -eq 0 ]; then
  echo "stop-conditions: no facts audited (the dump is empty)"
  exit 1
fi
if [ "$reports" -ne 0 ]; then
  echo "stop-conditions: $reports false-noalias report(s)"
  exit 1
fi
echo "stop-conditions: $facts facts audited, 0 false-noalias reports"
