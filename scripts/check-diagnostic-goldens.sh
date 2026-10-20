#!/bin/sh
# Phase 20: check that every diagnostic class in the corpus still produces
# its recorded wording.  Usage:
#
#   scripts/check-diagnostic-goldens.sh            check
#   scripts/check-diagnostic-goldens.sh --update   rewrite the goldens
#
# The corpus and its list live in gcc/testsuite/crug.dg/diagnostics.  Each
# top50.txt line is "<code> | cases/<name>.c | <flags> | <origin note>".
# A case is compiled with -fsyntax-only -fno-diagnostics-color from the
# corpus directory, so the file names inside the goldens stay stable.
#
# The fourth field names an origin note the compiler must also print ("-"
# means the class has no note).  That keeps the note column honest instead
# of decorative.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR="$ROOT/gcc/testsuite/crug.dg/diagnostics"
LIST="$DIR/top50.txt"
COMPILER="$ROOT/gcc/crugc"
UPDATE=0

if [ "${1:-}" = "--update" ]; then
  UPDATE=1
elif [ $# -gt 0 ]; then
  echo "usage: $0 [--update]" >&2
  exit 2
fi

if [ ! -f "$LIST" ]; then
  echo "check-diagnostic-goldens: missing $LIST" >&2
  exit 2
fi
if [ ! -x "$COMPILER" ]; then
  echo "check-diagnostic-goldens: missing compiler launcher $COMPILER" >&2
  exit 2
fi

PATH="$ROOT/gcc:$PATH"
export PATH

tmp=$(mktemp -d "${TMPDIR:-/tmp}/crug-goldens.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

lines=0
codes=0
ok=0
bad=0

# The list must be exactly 50 distinct codes and 50 distinct cases.
lines=$(grep -vc '^[[:space:]]*\(#\|$\)' "$LIST")
uniq_codes=$(grep -v '^[[:space:]]*\(#\|$\)' "$LIST" | cut -d'|' -f1 | sed 's/ *$//;s/^ *//' | sort -u | wc -l)
uniq_cases=$(grep -v '^[[:space:]]*\(#\|$\)' "$LIST" | cut -d'|' -f2 | sed 's/ *$//;s/^ *//' | sort -u | wc -l)

if [ "$lines" -ne 50 ] || [ "$uniq_codes" -ne 50 ] || [ "$uniq_cases" -ne 50 ]; then
  echo "check-diagnostic-goldens: top50.txt must hold 50 distinct codes and cases (have $lines lines, $uniq_codes codes, $uniq_cases cases)" >&2
  exit 1
fi

while IFS='|' read -r code case flags note; do
  case "$code" in
    ''|'#'*) continue ;;
  esac
  code=$(printf '%s' "$code" | sed 's/^ *//;s/ *$//')
  case=$(printf '%s' "$case" | sed 's/^ *//;s/ *$//')
  flags=$(printf '%s' "$flags" | sed 's/^ *//;s/ *$//')
  note=$(printf '%s' "$note" | sed 's/^ *//;s/ *$//')

  codes=$((codes + 1))

  golden="$DIR/golden/$code.txt"
  got="$tmp/$code.txt"

  ( cd "$DIR" && $COMPILER -fsyntax-only -fno-diagnostics-color $flags "$case" ) >/dev/null 2>"$got"

  if [ "$UPDATE" -eq 1 ]; then
    cp "$got" "$golden"
    ok=$((ok + 1))
    continue
  fi

  if ! grep -q "\[$code\]" "$got"; then
    echo "FAIL $code: the compiler did not print [$code] for $case" >&2
    bad=$((bad + 1))
    continue
  fi

  if [ "$note" != "-" ]; then
    if ! grep -qF "note: $note" "$got"; then
      echo "FAIL $code: missing origin note \"note: $note\"" >&2
      bad=$((bad + 1))
      continue
    fi
  fi

  if ! cmp -s "$got" "$golden"; then
    echo "FAIL $code: wording drifted (run $0 --update to refresh $golden)" >&2
    diff -u "$golden" "$got" | head -20 >&2
    bad=$((bad + 1))
    continue
  fi

  ok=$((ok + 1))
done < "$LIST"

if [ "$UPDATE" -eq 1 ]; then
  echo "check-diagnostic-goldens: wrote $ok golden(s)"
  exit 0
fi

echo "check-diagnostic-goldens: $ok case(s) match their golden, $bad drifted"
[ "$bad" -eq 0 ]
