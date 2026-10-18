#!/usr/bin/env bash
# The C_COMPAT regression suite (roadmap section 36).
#
# Phase 1 claims plain C compatibility. The suite compiles the corpus with
# crugc and the baseline xgcc at three optimization levels and compares exit
# status and object bytes, then adds the two phase-1 observable checks:
# contextual words remain ordinary identifiers, and no hidden ownership state
# is emitted. Any difference is a FAIL.
set -euo pipefail

cd "$(dirname "$0")/.."

fail=0
matrices=0

for opt in -O0 -O2 -Os; do
  matrices=$((matrices + 1))
  if ! scripts/diff-cc.sh corpus "$opt"; then
    fail=$((fail + 1))
  fi
done

if ! printf 'int own=1; int class_ref; int mut=2; int go=3; int scope=4; int match=5; int select=6;\n' \
     | gcc/crugc -x c -fsyntax-only -; then
  echo "C_COMPAT: contextual words are not ordinary identifiers" >&2
  fail=$((fail + 1))
fi

if ! scripts/check-no-hidden-state.sh; then
  fail=$((fail + 1))
fi

echo "C_COMPAT: $matrices option matrices plus 2 checks, $fail FAIL"
[ "$fail" -eq 0 ]
