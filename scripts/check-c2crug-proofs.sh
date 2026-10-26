#!/bin/sh
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
# Focused proof checks for the c2crug path classification.
#
# The two audit defects R4-TOOL-001 and R4-TOOL-002 came from local
# heuristics: a label or an else line hid an enclosing if, and a null guard
# was credited to an unrelated return.  These fixtures pin the fixed rule:
# only a release that starts a statement at function depth, with every early
# exit exactly covered by a null guard, may be migrated.
#
# 1. --safe classifies all eight fixtures as expected (raw, or own with the
#    guard confidence 0.80) and the report validates.
# 2. Raw fixtures come out byte-identical; the guarded one uses own/drop.
# 3. Originals and the transformed tree compile with gcc and crugc, and the
#    transformed programs behave like the originals.

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CRUGC=$ROOT/gcc/crugc
SRC=$ROOT/migration/proofs
TMP=$ROOT/build/c2crug-proofs
FLAGS="-O0 -fcrug-ownership -fcrug-borrow"

if [ -n "$CRUGC_OVERRIDE" ]; then
  CRUGC=$CRUGC_OVERRIDE
fi

fail() {
  echo "check-c2crug-proofs: FAIL: $*" >&2
  exit 1
}

[ -x "$CRUGC" ] || fail "no crugc at $CRUGC (build with make baseline first)"
[ -d "$SRC" ] || fail "no proof fixtures in $SRC"

rm -rf "$TMP"
mkdir -p "$TMP"

# 1. classify: --safe must leave every conditional path raw.
"$ROOT/c2crug" --safe "$SRC" -o "$TMP/out" --report "$TMP/report.json" \
  > "$TMP/safe.txt" || fail "c2crug --safe failed"
python3 "$ROOT/scripts/validate-c2crug-report.py" "$TMP/report.json" > /dev/null \
  || fail "the proof report is not valid"

python3 - "$TMP/report.json" <<'PY' || exit 1
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    report = json.load(f)

want = {
    "label.c": ("raw", False, None, ("conditional path", "label")),
    "else.c": ("raw", False, None, ("conditional path", "else")),
    "case.c": ("raw", False, None, ("conditional path", "switch case")),
    "unbraced-loop.c": ("raw", False, None, ("conditional path", "for")),
    "guard-unrelated.c": ("raw", False, None, ("early exit",)),
    "guard-valid.c": ("own", True, 0.80, ("null guard",)),
    "multi-declarator.c": ("raw", False, None, ("multiple declarators",)),
    "conditional.c": ("raw", False, None, ("conditional path",)),
}

by_file = {}
for entry in report["entries"]:
    by_file.setdefault(entry["file"], []).append(entry)

problems = []
for name in sorted(want):
    kind, applied, confidence, needles = want[name]
    entries = by_file.get(name)
    if not entries:
        problems.append("%s: no report entry" % name)
        continue
    if len(entries) != 1:
        problems.append("%s: expected one entry, got %d"
                        % (name, len(entries)))
        continue
    entry = entries[0]
    if entry["kind"] != kind:
        problems.append("%s: kind %s, want %s"
                        % (name, entry["kind"], kind))
    if entry["applied"] != applied:
        problems.append("%s: applied %s, want %s"
                        % (name, entry["applied"], applied))
    if confidence is not None and abs(entry["confidence"] - confidence) > 1e-9:
        problems.append("%s: confidence %.2f, want %.2f"
                        % (name, entry["confidence"], confidence))
    for needle in needles:
        if needle not in entry["reason"]:
            problems.append("%s: reason does not mention %r: %s"
                            % (name, needle, entry["reason"]))
for name in sorted(set(by_file) - set(want)):
    problems.append("%s: unexpected report entry" % name)
for problem in problems:
    print("check-c2crug-proofs: " + problem, file=sys.stderr)
sys.exit(1 if problems else 0)
PY

# 2. the raw fixtures are byte-identical, the guarded one is migrated.
for name in label else case unbraced-loop guard-unrelated multi-declarator \
            conditional; do
  diff -q "$SRC/$name.c" "$TMP/out/$name.c" > /dev/null \
    || fail "$name.c is raw in the report but was rewritten"
done
grep -q "own int \*p = malloc" "$TMP/out/guard-valid.c" \
  || fail "guard-valid.c did not become an own pointer"
grep -q "drop (p);" "$TMP/out/guard-valid.c" \
  || fail "guard-valid.c free was not rewritten to drop"
if grep -q "free (p);" "$TMP/out/guard-valid.c"; then
  fail "guard-valid.c still calls free"
fi

# 3. compile the originals and the transformed tree, then compare behaviour.
for src in "$SRC"/*.c; do
  name=$(basename "$src" .c)
  gcc -std=c99 -fsyntax-only "$src" \
    || fail "gcc rejected the original $name.c"
  "$CRUGC" $FLAGS -fsyntax-only "$src" \
    || fail "crugc rejected the original $name.c"
  gcc -std=c99 -O0 -o "$TMP/$name.orig" "$src" \
    || fail "gcc could not build the original $name.c"

  out="$TMP/out/$name.c"
  # Only the raw fixtures are still plain C; the migrated guard-valid.c uses
  # own/drop and is checked by crugc alone.
  if [ "$name" != "guard-valid" ]; then
    gcc -std=c99 -fsyntax-only "$out" \
      || fail "gcc rejected the transformed $name.c"
  fi
  "$CRUGC" $FLAGS -fsyntax-only "$out" \
    || fail "crugc rejected the transformed $name.c"
  "$CRUGC" $FLAGS -O0 -o "$TMP/$name.new" "$out" \
    || fail "crugc could not build the transformed $name.c"

  if "$TMP/$name.orig" > "$TMP/$name.orig.stdout" 2>&1; then
    orig_rc=0
  else
    orig_rc=$?
  fi
  if "$TMP/$name.new" > "$TMP/$name.new.stdout" 2>&1; then
    new_rc=0
  else
    new_rc=$?
  fi
  [ "$orig_rc" -eq "$new_rc" ] \
    || fail "$name changed the exit status ($orig_rc -> $new_rc)"
  diff -u "$TMP/$name.orig.stdout" "$TMP/$name.new.stdout" \
    > "$TMP/$name.diff" \
    || fail "$name changed its output"
done

echo "check-c2crug-proofs: 8 proof fixtures classified, compiled and run as expected"
