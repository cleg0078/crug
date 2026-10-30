#!/bin/sh
# Phase 20: the machine-readable diagnostic contract.  A Crug code must be
# the SARIF ruleId (not the -W option name), the rule must be listed in the
# run's reportingDescriptors with the docs URL, and an origin note must be a
# result of its own.  This is what editors and CI tools consume.
#
#   scripts/check-diagnostic-sarif.sh

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR="$ROOT/gcc/testsuite/crug.dg/diagnostics"
COMPILER="$ROOT/gcc/crugc"

if [ ! -x "$COMPILER" ]; then
  echo "check-diagnostic-sarif: missing compiler launcher $COMPILER" >&2
  exit 2
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/crug-sarif.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cd "$tmp" || exit 2

check_error_case ()
{
  rm -f -- *.sarif
  "$COMPILER" -fsyntax-only -fdiagnostics-format=sarif-file \
    -fcrug-ownership "$DIR/cases/use-after-move.c" >/dev/null 2>&1
  sarif=$(ls -- *.sarif 2>/dev/null | head -n 1)
  if [ -z "$sarif" ]; then
    echo "check-diagnostic-sarif: no SARIF file for the error case" >&2
    return 1
  fi
  python3 - "$sarif" <<'PY'
import json
import sys

with open (sys.argv[1], encoding="utf-8") as f:
    doc = json.load (f)
run = doc["runs"][0]
ids = [r.get ("ruleId") for r in run["results"]]
texts = [r["message"]["text"] for r in run["results"]]
if "crug-UAM-001" not in ids:
    sys.exit ("ruleId is not crug-UAM-001: %r" % (ids,))
if not any ("use of moved value" in t for t in texts):
    sys.exit ("missing the error result: %r" % (texts,))
if not any ("value was moved here" in t for t in texts):
    sys.exit ("origin note is not a result: %r" % (texts,))
rules = [r["id"] for r in run["tool"]["driver"].get ("rules", [])]
if "crug-UAM-001" not in rules:
    sys.exit ("crug-UAM-001 is not in reportingDescriptors: %r" % (rules,))
if not any ("docs/crug/DIAGNOSTICS.md" in (r.get ("helpUri") or "")
            for r in run["tool"]["driver"]["rules"]):
    sys.exit ("the rule has no documentation URL")
print ("sarif: ruleId crug-UAM-001, origin note present, rule documented")
PY
}

check_warning_case ()
{
  rm -f -- *.sarif
  "$COMPILER" -fsyntax-only -fdiagnostics-format=sarif-file \
    -fcrug-ownership "$DIR/cases/unadopted-own.c" >/dev/null 2>&1
  sarif=$(ls -- *.sarif 2>/dev/null | head -n 1)
  if [ -z "$sarif" ]; then
    echo "check-diagnostic-sarif: no SARIF file for the warning case" >&2
    return 1
  fi
  python3 - "$sarif" <<'PY'
import json
import sys

with open (sys.argv[1], encoding="utf-8") as f:
    doc = json.load (f)
ids = [r.get ("ruleId") for r in doc["runs"][0]["results"]]
if "crug-OWN-007" not in ids:
    sys.exit ("a warning with a code must use the code as ruleId: %r" % (ids,))
print ("sarif: warning ruleId crug-OWN-007")
PY
}

# A .crugffi diagnostic is emitted at UNKNOWN_LOCATION with the sidecar path
# inside the message, and must still carry the stable code as ruleId and the
# documentation URL (fix-p3, FFI-02).
check_ffi_case ()
{
  rm -f -- *.sarif
  "$COMPILER" -fsyntax-only -fdiagnostics-format=sarif-file \
    -fcrug-ffi="$ROOT/gcc/testsuite/crug.dg/ffi/metadata/eof-type.crugffi" \
    "$ROOT/gcc/testsuite/crug.dg/ffi/metadata/eof-type.c" >/dev/null 2>&1
  sarif=$(ls -- *.sarif 2>/dev/null | head -n 1)
  if [ -z "$sarif" ]; then
    echo "check-diagnostic-sarif: no SARIF file for the FFI case" >&2
    return 1
  fi
  python3 - "$sarif" <<'PY'
import json
import sys

with open (sys.argv[1], encoding="utf-8") as f:
    doc = json.load (f)
run = doc["runs"][0]
ids = [r.get ("ruleId") for r in run["results"]]
texts = [r["message"]["text"] for r in run["results"]]
if "crug-FFI-013" not in ids:
    sys.exit ("FFI ruleId is not crug-FFI-013: %r" % (ids,))
if not any ("unexpected end of file" in t for t in texts):
    sys.exit ("missing the FFI-013 result: %r" % (texts,))
rules = [r for r in run["tool"]["driver"].get ("rules", [])
         if r["id"] == "crug-FFI-013"]
if not rules:
    sys.exit ("crug-FFI-013 is not in reportingDescriptors")
if not (rules[0].get ("helpUri") or "").endswith ("docs/crug/DIAGNOSTICS.md"):
    sys.exit ("the FFI rule has no documentation URL: %r" % (rules[0],))
print ("sarif: ruleId crug-FFI-013, sidecar path in the message, rule documented")
PY
}

check_error_case || exit 1
check_warning_case || exit 1
check_ffi_case || exit 1
echo "check-diagnostic-sarif: ok"
