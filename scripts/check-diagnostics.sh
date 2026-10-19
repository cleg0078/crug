#!/usr/bin/env bash
# check-diagnostics.sh - validate the diagnostic-code registry.
#
# Checks:
#   1. docs/crug/DIAGNOSTICS.md exists and contains registry rows;
#   2. every crug-<AREA>-<NNN> code referenced under docs/ or spec/ is registered;
#   3. every referenced code uses a known area;
#   4. no code is registered twice.
#
# Exit: 0 all checks pass, 1 registry defect.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
reg="$root/docs/crug/DIAGNOSTICS.md"
areas='UAM|OWN|BORROW|DROP|CONC|ADT|TYPE|GRAMMAR|SAFE|FFI|KERNEL'

[ -f "$reg" ] || { echo "check-diagnostics: missing $reg" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Registry rows are table rows whose first cell is a code. The opening
# backtick is built at run time so this script stays free of it.
bt=$(printf '\140')
grep -E "^[|][[:space:]]*($bt)?crug-" "$reg" \
  | grep -oE 'crug-[A-Z]+-[0-9]{3}' | LC_ALL=C sort > "$tmp/registered" || true

[ -s "$tmp/registered" ] || { echo "check-diagnostics: no registry rows found in $reg" >&2; exit 1; }

# Every code-shaped token referenced in the docs and the specs.
grep -rhoE 'crug-[A-Z]+-[0-9]{3}' "$root/docs" "$root/spec" --include='*.md' 2>/dev/null \
  | LC_ALL=C sort -u > "$tmp/referenced_all" || true

grep -E "crug-($areas)-[0-9]{3}" "$tmp/referenced_all" > "$tmp/referenced" || true
grep -vE "crug-($areas)-[0-9]{3}" "$tmp/referenced_all" > "$tmp/unknown" || true

LC_ALL=C uniq -d "$tmp/registered" > "$tmp/dups" || true
LC_ALL=C comm -23 "$tmp/referenced" "$tmp/registered" > "$tmp/missing" || true

fail=0
if [ -s "$tmp/unknown" ]; then
  echo 'check-diagnostics: referenced code(s) with an unknown area:' >&2
  sed 's/^/  /' "$tmp/unknown" >&2
  fail=1
fi
if [ -s "$tmp/dups" ]; then
  echo 'check-diagnostics: duplicate registry row(s):' >&2
  sed 's/^/  /' "$tmp/dups" >&2
  fail=1
fi
if [ -s "$tmp/missing" ]; then
  echo 'check-diagnostics: referenced but not registered:' >&2
  sed 's/^/  /' "$tmp/missing" >&2
  fail=1
fi

if [ "$fail" -ne 0 ]; then exit 1; fi

registered=$(wc -l < "$tmp/registered")
referenced=$(wc -l < "$tmp/referenced")
echo "check-diagnostics: $registered registered code(s); $referenced referenced code(s); all references registered."
