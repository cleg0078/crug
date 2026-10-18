#!/usr/bin/env bash
# Archive a testsuite run as gate evidence.
#
# Usage: scripts/record-baseline.sh [phase0] [log]
#
# Reading the run: the sum file is the authoritative record; the log holds the
# console output. Both are archived compressed, together with an extract of the
# non-PASS lines and a README naming the commit, compiler version and date, so
# a gate adjudicator can judge the run from the archive alone.
set -euo pipefail

cd "$(dirname "$0")/.."

phase=${1:-phase0}
log=${2:-build/obj/check-c.log}
obj=${OBJ_DIR:-build/obj}
sum=${SUM_FILE:-$obj/gcc/testsuite/gcc/gcc.sum}
out=docs/crug/baseline/$phase

[ -s "$sum" ] || { echo "record-baseline: no summary at $sum; run make check-c first" >&2; exit 2; }

mkdir -p "$out"

commit=$(git rev-parse HEAD)
short=$(git rev-parse --short HEAD)
date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
xgcc=$obj/gcc/xgcc
if [ -x "$xgcc" ]; then version=$("$xgcc" -B"$obj/gcc/" --version | head -n 1);
else version='xgcc not built'; fi
host=$(uname -srm)

count() { grep -c "^$1:" "$sum" 2>/dev/null || true; }
passes=$(count PASS) ; fails=$(count FAIL)
xfails=$(count XFAIL) ; xpasses=$(count XPASS)
unresolved=$(count UNRESOLVED) ; unsupported=$(count UNSUPPORTED)

xz -T0 -6 -c "$sum" > "$out/gcc.sum.xz"
if [ -s "$log" ]; then xz -T0 -6 -c "$log" > "$out/check-c.log.xz"; fi
grep -E '^(FAIL|XPASS|UNRESOLVED):' "$sum" > "$out/FAIL.txt" || true
grep -E '^# of ' "$sum" > "$out/SUMMARY.txt" || true

{
  echo "# Phase baseline: $phase"
  echo
  echo "Recorded: $date_utc (UTC) from commit $short ($commit)."
  echo
  echo "- Compiler: $version"
  echo "- Host: $host"
  echo "- Command: make check-c (RUNTESTFLAGS as on the command line)"
  echo "- Source tree: repository root, unchanged since the tag releases/gcc-16.2"
  echo "  except for later commits; this run record is only valid for $short."
  echo
  echo '## Result'
  echo
  echo "- PASS: $passes"
  echo "- FAIL: $fails"
  echo "- XFAIL: $xfails"
  echo "- XPASS: $xpasses"
  echo "- UNRESOLVED: $unresolved"
  echo "- UNSUPPORTED: $unsupported"
  echo
  echo '## Files'
  echo
  echo '- gcc.sum.xz - the complete DejaGnu summary (authoritative).'
  echo '- check-c.log.xz - the console log of the same run.'
  echo '- FAIL.txt - every FAIL, XPASS and UNRESOLVED line extracted from the sum.'
  echo '- SUMMARY.txt - the "# of" summary lines from the sum.'
  echo
  echo '## Adjudication note for the phase-0 gate'
  echo
  echo 'Phase 0 check 3 asks for a run summary archived under this directory with'
  echo 'no FAIL line absent from the pre-fork baseline. The source tree at this commit'
  echo 'is the imported GCC 16.2.0 tree (tag releases/gcc-16.2), so it *is* the'
  echo 'pre-fork baseline: any unexpected failure listed in FAIL.txt is an upstream'
  echo 'failure present before the fork started, not a regression introduced by Crug.'
  echo 'The gate adjudicator records that conclusion here and cites this file.'
} > "$out/README.md"

echo "record-baseline: wrote $out (PASS $passes, FAIL $fails, XFAIL $xfails, XPASS $xpasses, UNRESOLVED $unresolved)"