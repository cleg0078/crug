#!/bin/sh
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
# Compare a C source tree with the tree c2crug --safe produced.
#
# Usage: scripts/c2crug-diff.sh SRC OUT [MAX_PERCENT]
#
# The Phase 12 gate (EXIT-GATES, amendment A-15) requires that a safe-mode
# migration changes at most 20 percent of the source lines and preserves the
# number of comment lines, so comments and source structure survive.

set -e

if [ "$#" -lt 2 ]; then
  echo "usage: $0 SRC OUT [MAX_PERCENT]" >&2
  exit 2
fi

SRC=$1
OUT=$2
MAX=20
if [ "$#" -ge 3 ]; then
  MAX=$3
fi

if [ ! -d "$SRC" ] || [ ! -d "$OUT" ]; then
  echo "c2crug-diff: source and output must be directories" >&2
  exit 2
fi

total=0
changed=0

files=$(cd "$SRC" && find . -name '*.c' | sort)
for rel in $files; do
  srcf="$SRC/$rel"
  outf="$OUT/$rel"
  if [ ! -f "$outf" ]; then
    echo "c2crug-diff: missing output file $outf" >&2
    exit 1
  fi
  total=$((total + $(wc -l < "$srcf")))
  changed=$((changed + $(diff -U0 "$srcf" "$outf" | sed -n '/^---/d; /^-/p' | wc -l)))
done

comment_lines() {
  find "$1" -name '*.c' -exec grep -hE '^[[:space:]]*(/\*|\*|//)' {} + 2>/dev/null | wc -l
}

src_comments=$(comment_lines "$SRC")
out_comments=$(comment_lines "$OUT")

if [ "$total" -eq 0 ]; then
  echo "c2crug-diff: no source lines found under $SRC" >&2
  exit 2
fi

percent=$(awk -v c="$changed" -v t="$total" 'BEGIN { printf "%.1f", 100.0 * c / t }')

if [ "$src_comments" -ne "$out_comments" ]; then
  echo "c2crug-diff: comment lines changed: $src_comments -> $out_comments" >&2
  exit 1
fi
if [ $((changed * 100)) -gt $((total * MAX)) ]; then
  echo "c2crug-diff: $changed of $total line(s) changed ($percent% > $MAX%)" >&2
  exit 1
fi

echo "c2crug-diff: $changed of $total source line(s) changed ($percent% <= $MAX%), comment lines $src_comments -> $out_comments"
