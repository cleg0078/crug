#!/usr/bin/env bash
# Compile a corpus twice, with two different flag sets and one compiler, and
# compare the object bytes.
#
# Usage: scripts/diff-obj.sh [corpus-dir] ["flags A"] ["flags B"]
#
# This is the phase-3 and phase-4 codegen-neutrality check: a Crug option is
# allowed to change what the front end accepts, never what the back end emits
# (ADR 0003 D5, ADR 0006 D2/D4, RFC-0001 section 22).  Defaults: corpus/,
# "-O2" and "-O2 -fcrug-modern".
set -uo pipefail

cd "$(dirname "$0")/.."

corpus=${1:-corpus}
flags_a=${2:--O2}
flags_b=${3:--O2 -fcrug-modern}

cc=${CRUGC:-gcc/crugc}

[ -d "$corpus" ] || { echo "diff-obj: no corpus directory: $corpus" >&2; exit 2; }
[ -x "$cc" ] || { echo "diff-obj: crugc not executable: $cc" >&2; exit 2; }

read -r -a a <<< "$flags_a"
read -r -a b <<< "$flags_b"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/a" "$work/b"

files=0
diffs=0

while IFS= read -r src; do
  files=$((files + 1))
  name=$(basename "$src" .c)

  set +e
  "$cc" "${a[@]}" -c "$src" -o "$work/a/$name.o" \
    > "$work/a/$name.stdout" 2> "$work/a/$name.stderr"
  rc_a=$?
  "$cc" "${b[@]}" -c "$src" -o "$work/b/$name.o" \
    > "$work/b/$name.stdout" 2> "$work/b/$name.stderr"
  rc_b=$?
  set -e

  if [ "$rc_a" -ne "$rc_b" ]; then
    echo "diff-obj: exit status differs for $src: A=$rc_a B=$rc_b" >&2
    diffs=$((diffs + 1))
    continue
  fi

  if [ "$rc_a" -ne 0 ]; then
    echo "diff-obj: both flag sets failed for $src" >&2
    diffs=$((diffs + 1))
    continue
  fi

  if ! cmp -s "$work/a/$name.o" "$work/b/$name.o"; then
    echo "diff-obj: object code differs for $src" >&2
    diffs=$((diffs + 1))
  fi
done < <(find "$corpus" -name '*.c' -type f | sort)

if [ "$files" -eq 0 ]; then
  echo "diff-obj: no .c files under $corpus" >&2
  exit 2
fi

echo "diff-obj: $files files, $diffs differing ($flags_a | $flags_b)"
[ "$diffs" -eq 0 ]
