#!/bin/sh
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
# End-to-end check of the Phase 12 c2crug migration pipeline.
#
# 1. --analyze reports ambiguous ownership as raw and changes nothing.
# 2. --safe copies the tree and applies only the proven transformations.
# 3. The report validates against the published schema.
# 4. The transformed tree compiles with crugc and needs no C change.
# 5. The transformed programs produce exactly the original output.
# 6. Source churn is within the gate band and comments are preserved.

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CRUGC=$ROOT/gcc/crugc
if [ -n "$CRUGC_OVERRIDE" ]; then
  CRUGC=$CRUGC_OVERRIDE
fi
TMP=$ROOT/build/c2crug-check
SRC=$ROOT/migration
FLAGS="-O2 -fcrug-ownership -fcrug-borrow -fcrug-ffi=libc.crugffi"

fail() {
  echo "c2crug: FAIL: $*" >&2
  exit 1
}

if [ ! -x "$CRUGC" ]; then
  fail "no crugc at $CRUGC (build with make baseline first)"
fi

rm -rf "$TMP"
mkdir -p "$TMP"

# 1. analyze: ambiguous ownership is raw, nothing is applied or written.
"$ROOT/c2crug" --analyze "$SRC" --report "$TMP/analyze.json" > "$TMP/analyze.txt" \
  || fail "c2crug --analyze failed"
grep -q "raw confidence" "$TMP/analyze.txt" \
  || fail "analyze reported no raw entry"
grep -q "borrow-suggestion" "$TMP/analyze.txt" \
  || fail "analyze reported no borrow suggestion"
grep -q "not applied" "$TMP/analyze.txt" \
  || fail "analyze did not mark its findings as not applied"
python3 "$ROOT/scripts/validate-c2crug-report.py" "$TMP/analyze.json" >/dev/null \
  || fail "the analyze report is not valid"

# 2. safe: copy and transform only the proven pointers.
"$ROOT/c2crug" --safe "$SRC" -o "$TMP/out" --report "$TMP/safe.json" > "$TMP/safe.txt" \
  || fail "c2crug --safe failed"

# 3. validate the report.
python3 "$ROOT/scripts/validate-c2crug-report.py" "$TMP/safe.json" > "$TMP/report.txt" \
  || fail "the safe report is not valid"

# 4. the transformed tree compiles with crugc, no C change required.
n=0
for f in "$TMP"/out/*.c; do
  (cd "$TMP/out" && "$CRUGC" $FLAGS -c "$(basename "$f")" -o "$(basename "$f" .c).o") \
    || fail "crugc rejected $(basename "$f")"
  n=$((n + 1))
done
[ "$n" -ge 8 ] || fail "expected the migration corpus, compiled only $n file(s)"

# 5. differential run: the transformed programs print the original output.
make -C "$TMP/out" test CC="$CRUGC" CFLAGS="$FLAGS" > "$TMP/crug.log" 2>&1 \
  || { tail -20 "$TMP/crug.log" >&2; fail "the migrated programs failed their tests"; }
grep -q "migration: 8 programs pass" "$TMP/crug.log" \
  || fail "the migrated programs did not all pass"

cp -R "$SRC" "$TMP/plain"
make -C "$TMP/plain" clean all test > "$TMP/plain.log" 2>&1 \
  || { tail -20 "$TMP/plain.log" >&2; fail "the original corpus failed its tests"; }
grep -q "migration: 8 programs pass" "$TMP/plain.log" \
  || fail "the original corpus did not all pass"

# 6. churn and comment preservation, and the report must match the output.
"$ROOT/scripts/c2crug-diff.sh" "$SRC" "$TMP/out" 20 > "$TMP/diff.txt" \
  || { cat "$TMP/diff.txt" >&2; fail "source churn or comments outside the gate band"; }

python3 - "$TMP/safe.json" "$SRC" "$TMP/out" <<'PY' || fail "report and output tree disagree"
import json
import os
import sys

report_path, src_root, out_root = sys.argv[1:4]
with open(report_path, "r", encoding="utf-8") as f:
    report = json.load(f)
applied = 0
for entry in report["entries"]:
    rel = entry["file"]

    def line(root, number):
        with open(os.path.join(root, rel), "r", encoding="utf-8",
                  errors="surrogateescape") as f:
            return f.read().split("\n")[number - 1]

    source = line(src_root, entry["line"])
    output = line(out_root, entry["line"])
    if entry["applied"]:
        applied += 1
        body = source.lstrip()
        indent = " " * (len(source) - len(body))
        if not output.startswith(indent + "own "):
            print("c2crug: %s:%d is applied but has no own"
                  % (rel, entry["line"]), file=sys.stderr)
            sys.exit(1)
        rel_line = entry["transformation"]["release_line"]
        if "drop (" not in line(out_root, rel_line):
            print("c2crug: %s:%d has no drop" % (rel, rel_line),
                  file=sys.stderr)
            sys.exit(1)
    elif output != source:
        print("c2crug: %s:%d is not applied but changed"
              % (rel, entry["line"]), file=sys.stderr)
        sys.exit(1)
print("c2crug: %d applied entr(ies) match the output tree" % applied)
PY

own=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["counts"]["own"])' "$TMP/safe.json")
raw=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["counts"]["raw"])' "$TMP/safe.json")

echo "c2crug: $own own applied, $raw raw preserved, 8/8 programs pass, $(cat "$TMP/diff.txt")"
