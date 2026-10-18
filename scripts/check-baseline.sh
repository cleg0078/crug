#!/usr/bin/env bash
# Validate an archived testsuite baseline as gate evidence.
#
# Usage: scripts/check-baseline.sh [docs/crug/baseline/phase0]
#
# The phase-0 gate does not re-run a two-hour suite on every adjudication; it
# checks the archive instead. This script proves that the compressed summary,
# the non-PASS extract and the recorded failure allowlist still agree, and that
# no failing test is outside the allowlist. A new failure therefore fails the
# gate; the allowlist can only grow by amending the archive together with a
# written reason, which is a reviewable commit.
set -euo pipefail

cd "$(dirname "$0")/.."

dir=${1:-docs/crug/baseline/phase0}
sum=$dir/gcc.sum.xz
fail_list=$dir/FAIL.txt
allow=$dir/known-fail.txt

[ -s "$sum" ] || { echo "check-baseline: missing $sum" >&2; exit 2; }
[ -s "$fail_list" ] || { echo "check-baseline: missing $fail_list" >&2; exit 2; }
[ -s "$allow" ] || { echo "check-baseline: missing $allow" >&2; exit 2; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

xz -dc "$sum" > "$tmp/sum"
grep -E '^(FAIL|XPASS|UNRESOLVED):' "$tmp/sum" > "$tmp/nonpass" || true

if ! cmp -s "$tmp/nonpass" "$fail_list"; then
  echo "check-baseline: $fail_list does not match the archived summary" >&2
  diff -u "$fail_list" "$tmp/nonpass" | head -20 >&2 || true
  exit 1
fi

bad=0
while IFS= read -r line; do
  case $line in
    FAIL:*) ;;
    *) continue ;;
  esac
  path=${line#FAIL: }
  path=${path%% *}
  known=0
  while IFS= read -r pattern; do
    case $pattern in ''|'#'*) continue ;; esac
    if [[ $path == $pattern ]]; then known=1; break; fi
  done < "$allow"
  if [ "$known" = 0 ]; then
    echo "check-baseline: failure not in $allow: $path" >&2
    bad=1
  fi
done < "$tmp/sum"
[ "$bad" = 0 ] || exit 1

count() { grep -c "^$1:" "$tmp/sum" || true; }
printf 'baseline phase0: PASS %s FAIL %s XPASS %s XFAIL %s UNRESOLVED %s UNSUPPORTED %s\n' \
  "$(count PASS)" "$(count FAIL)" "$(count XPASS)" "$(count XFAIL)" \
  "$(count UNRESOLVED)" "$(count UNSUPPORTED)"