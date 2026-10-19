#!/bin/sh
# fork-delta: inventory of the Crug fork against its pinned GCC vendor tag.
#
# Reads the repository only; no network. It separates the two deltas that an
# upgrade has to handle:
#   - files upstream also owns: our hunks there must be replayed (conflicts)
#   - files only Crug has: they move with the tree and never conflict
#
# Usage:
#   scripts/fork-delta.sh [--tag REF] [--quiet]
#
# The reference tag defaults to releases/gcc-16.2 and can be overridden with
# CRUG_VENDOR_TAG or --tag. Exit status: 0 on success, 2 when the tag is
# missing or the arguments are bad.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

TAG=releases/gcc-16.2
if [ -n "$(printenv CRUG_VENDOR_TAG)" ]; then
    TAG=$(printenv CRUG_VENDOR_TAG)
fi
quiet=0

while [ $# -gt 0 ]; do
    case $1 in
    --tag)
        if [ $# -lt 2 ]; then
            echo "fork-delta: --tag needs a ref" >&2
            exit 2
        fi
        TAG=$2
        shift 2
        ;;
    --tag=*)
        TAG=$1
        TAG=${TAG#--tag=}
        shift
        ;;
    --quiet)
        quiet=1
        shift
        ;;
    -h|--help)
        sed -n '2,16p' "$0"
        exit 0
        ;;
    *)
        echo "fork-delta: unknown argument: $1" >&2
        exit 2
        ;;
    esac
done

if ! git rev-parse --verify --quiet "$TAG" >/dev/null; then
    echo "fork-delta: no such ref: $TAG" >&2
    exit 2
fi

TAG_SHA=$(git rev-parse --short=12 "$TAG")
HEAD_SHA=$(git rev-parse --short=12 HEAD)
COMMITS=$(git rev-list --count "$TAG"..HEAD)
MODIFIED=$(git diff --name-only --diff-filter=M "$TAG"..HEAD -- gcc/)
ADDED=$(git diff --name-only --diff-filter=A "$TAG"..HEAD -- gcc/)

count_lines() {
    printf '%s\n' "$1" | awk 'NF { n++ } END { print n + 0 }'
}

MODIFIED_N=$(count_lines "$MODIFIED")
ADDED_N=$(count_lines "$ADDED")

echo "fork-delta: base $TAG ($TAG_SHA), HEAD $HEAD_SHA, $COMMITS commits since the tag"

upstream_ins=0
upstream_del=0
upstream_hunks=0
row=0

if [ "$MODIFIED_N" -gt 0 ] && [ "$quiet" -eq 0 ]; then
    printf '\n%s\n' "upstream files modified by the fork (these hunks are replayed on a new baseline):"
    printf '%-46s %6s %6s %6s\n' "file" "hunks" "+ins" "-del"
fi

for f in $MODIFIED; do
    num=$(git diff --numstat "$TAG"..HEAD -- "$f" | awk 'NR == 1 { print $1 " " $2 }')
    ins=$(printf '%s\n' "$num" | awk '{ print $1 }')
    del=$(printf '%s\n' "$num" | awk '{ print $2 }')
    hunks=$(git diff -U0 "$TAG"..HEAD -- "$f" | grep -c '^@@')
    upstream_ins=$((upstream_ins + ins))
    upstream_del=$((upstream_del + del))
    upstream_hunks=$((upstream_hunks + hunks))
    if [ "$quiet" -eq 0 ]; then
        printf '%-46s %6s %6s %6s\n' "$f" "$hunks" "$ins" "$del"
    fi
    row=$((row + 1))
done

if [ "$MODIFIED_N" -gt 0 ]; then
    echo "  ---- totals: $MODIFIED_N files, $upstream_hunks hunks, +$upstream_ins/-$upstream_del"
fi

new_files=0
new_lines=0
echo
if [ "$ADDED_N" -gt 0 ]; then
    printf '%s\n' "files only Crug has (no upstream overlap, they move with the tree):"
    printf '%-46s %6s\n' "file" "lines"
fi

for f in $ADDED; do
    case $f in
    gcc/testsuite/*) continue ;;
    esac
    n=$(git show "HEAD:$f" | wc -l)
    new_files=$((new_files + 1))
    new_lines=$((new_lines + n))
    if [ "$quiet" -eq 0 ]; then
        printf '%-46s %6s\n' "$f" "$n"
    fi
done

echo
if [ "$quiet" -eq 0 ]; then
    tests=$(git diff --name-only --diff-filter=A "$TAG"..HEAD -- gcc/testsuite/ | wc -l)
    echo "  ---- $new_files new sources, $new_lines lines; $tests new testsuite files not listed"
fi

printf 'fork-delta: %s upstream files, %s hunks, +%s/-%s; %s new files, %s lines\n' \
    "$MODIFIED_N" "$upstream_hunks" "$upstream_ins" "$upstream_del" "$new_files" "$new_lines"

exit 0
