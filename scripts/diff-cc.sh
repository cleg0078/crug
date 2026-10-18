#!/usr/bin/env bash
# Compile a corpus with crugc and with the baseline xgcc and compare results.
#
# Usage: scripts/diff-cc.sh [corpus-dir] [compiler flags...]
#
# For every *.c under the corpus directory the script compiles the file once
# with each driver, then compares the exit status and the object file bytes.
# This is the phase-1 driver-drift check: crugc is allowed to differ from xgcc
# only when the difference is intended and documented.
set -euo pipefail

cd "$(dirname "$0")/.."

corpus=${1:-corpus}
shift || true

if [ "$#" -eq 0 ]; then
  set -- -O2
fi
flags=("$@")

cc=${CRUGC:-gcc/crugc}
base=${XGCC:-build/obj/gcc/xgcc}

[ -d "$corpus" ] || { echo "diff-cc: no corpus directory: $corpus" >&2; exit 2; }
[ -x "$cc" ] || { echo "diff-cc: crugc not executable: $cc" >&2; exit 2; }
[ -x "$base" ] || { echo "diff-cc: baseline xgcc not executable: $base" >&2; exit 2; }

base_dir=$(cd "$(dirname "$base")" && pwd)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

files=0
diffs=0

while IFS= read -r src; do
  files=$((files + 1))
  name=$(basename "$src" .c)
  mkdir -p "$work/base" "$work/crug"

  set +e
  "$base" -B"$base_dir/" "${flags[@]}" -c "$src" -o "$work/base/$name.o" \
    > "$work/base/$name.stdout" 2> "$work/base/$name.stderr"
  rc_base=$?
  "$cc" "${flags[@]}" -c "$src" -o "$work/crug/$name.o" \
    > "$work/crug/$name.stdout" 2> "$work/crug/$name.stderr"
  rc_crug=$?
  set -e

  if [ "$rc_base" -ne "$rc_crug" ]; then
    echo "diff-cc: exit status differs for $src: xgcc=$rc_base crugc=$rc_crug" >&2
    diffs=$((diffs + 1))
    continue
  fi

  if [ "$rc_base" -ne 0 ]; then
    continue
  fi

  if ! cmp -s "$work/base/$name.o" "$work/crug/$name.o"; then
    echo "diff-cc: object code differs for $src" >&2
    diffs=$((diffs + 1))
    continue
  fi

  if ! cmp -s "$work/base/$name.stdout" "$work/crug/$name.stdout"; then
    echo "diff-cc: stdout differs for $src" >&2
    diffs=$((diffs + 1))
  fi
done < <(find "$corpus" -name '*.c' -type f | sort)

if [ "$files" -eq 0 ]; then
  echo "diff-cc: no .c files under $corpus" >&2
  exit 2
fi

echo "diff-cc: $files files, $diffs differing (${flags[*]})"
[ "$diffs" -eq 0 ]
