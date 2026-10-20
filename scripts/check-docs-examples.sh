#!/bin/sh
# check-docs-examples.sh - compile and run the examples in MIGRATION-EXAMPLES.md.
#
# The page docs/crug/MIGRATION-EXAMPLES.md marks each executable program with
# an HTML comment of the form
#
#   <!-- example: NAME flags: -fcrug-... -->
#
# followed by a c fenced block and, after it, "<!-- expect -->" and a text
# fenced block holding the exact stdout. This script extracts those blocks,
# compiles each program in a scratch directory under build/, runs it, and
# compares stdout byte for byte. Nothing in the page is trusted to be current:
# a stale example is a failure.
#
# A runtime example (a flag list containing -fcrug-goroutines or
# -fcrug-channels) links the Linux runtime objects of libcrug, which are built
# on demand with make -C libcrug PAL=linux.
#
# Usage: scripts/check-docs-examples.sh
# Exit: 0 every example passed, 1 at least one example failed, 2 setup error
#       (missing page, compiler or runtime, or a malformed marked block).

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOC=$ROOT/docs/crug/MIGRATION-EXAMPLES.md
SCRATCH=$ROOT/build/docs-examples
LIB=$ROOT/libcrug
RUNTIME="$LIB/build/linux/task.o $LIB/build/linux/pal_linux.o $LIB/build/linux/switch.o $LIB/build/linux/stack.o $LIB/build/linux/chan.o"
PATH=$ROOT/gcc:$PATH
export PATH

case $SCRATCH in
  "$ROOT"/build/*) ;;
  *) echo "check-docs-examples: refusing scratch directory $SCRATCH" >&2; exit 2 ;;
esac

if [ ! -f "$DOC" ]; then
  echo "check-docs-examples: missing $DOC" >&2
  exit 2
fi
if ! command -v crugc >/dev/null 2>&1; then
  echo "check-docs-examples: no crugc on PATH (looked in $ROOT/gcc)" >&2
  exit 2
fi

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH" || { echo "check-docs-examples: cannot create $SCRATCH" >&2; exit 2; }

# Extract the marked examples. The fence is built from the backtick character
# so that this script stays free of a literal triple backtick.
awk -v dir="$SCRATCH" -v list="$SCRATCH/examples.list" '
function fail (msg) {
  printf "check-docs-examples: %s\n", msg > "/dev/stderr"
  bad = 1
  exit 2
}
BEGIN {
  bt = sprintf("%c", 96)
  fence = bt bt bt
  state = 0
  bad = 0
  n = 0
  printf "" > list
}
/^<!-- example: / {
  if (state != 0)
    fail("nested example marker at line " NR)
  line = $0
  sub(/^<!-- example: /, "", line)
  sub(/ -->[ \t]*$/, "", line)
  name = line
  sub(/[ \t].*/, "", name)
  if (name == "")
    fail("empty example name at line " NR)
  rest = line
  sub(/^[^ \t]+[ \t]+/, "", rest)
  if (rest !~ /^flags:/)
    fail("example " name " has no flags clause at line " NR)
  sub(/^flags:[ \t]*/, "", rest)
  flags = rest
  if (flags == "")
    fail("example " name " has an empty flag list")
  n++
  file = sprintf("%s/%d.c", dir, n)
  expfile = sprintf("%s/%d.expected", dir, n)
  flagsfile = sprintf("%s/%d.flags", dir, n)
  printf "%s\n", flags > flagsfile
  printf "%s %s %s\n", n, name, flags >> list
  printf "" > file
  printf "" > expfile
  state = 1
  next
}
state == 1 && $0 == fence "c" { state = 2; next }
state == 1 && $0 ~ /^[ \t]*$/ { next }
state == 1 { fail("expected a c fence after example " name " at line " NR) }
state == 2 && $0 == fence { state = 3; next }
state == 2 { print $0 >> file; next }
state == 3 && $0 == "<!-- expect -->" { state = 4; next }
state == 3 && $0 ~ /^[ \t]*$/ { next }
state == 3 { fail("expected the expect marker after example " name " at line " NR) }
state == 4 && $0 == fence "text" { state = 5; next }
state == 4 && $0 ~ /^[ \t]*$/ { next }
state == 4 { fail("expected a text fence after example " name " at line " NR) }
state == 5 && $0 == fence { state = 0; next }
state == 5 { print $0 >> expfile; next }
END {
  if (!bad && state != 0) {
    printf "check-docs-examples: unterminated example at end of %s\n", "the page" > "/dev/stderr"
    bad = 1
  }
  if (!bad && n == 0) {
    printf "check-docs-examples: no marked examples in %s\n", "the page" > "/dev/stderr"
    bad = 1
  }
  if (bad)
    exit 2
}
' "$DOC" || exit 2

need_runtime=0
while IFS=' ' read -r n name flags; do
  case " $flags " in
    *" -fcrug-goroutines "*|*" -fcrug-channels "*) need_runtime=1 ;;
  esac
done < "$SCRATCH/examples.list"

if [ "$need_runtime" -eq 1 ]; then
  missing=0
  for obj in $RUNTIME; do
    if [ ! -f "$obj" ]; then
      missing=1
    fi
  done
  if [ "$missing" -eq 1 ]; then
    echo "check-docs-examples: building the libcrug runtime objects (PAL=linux)" >&2
    if ! make -C "$LIB" PAL=linux > "$SCRATCH/runtime.log" 2>&1; then
      echo "check-docs-examples: make -C libcrug PAL=linux failed" >&2
      cat "$SCRATCH/runtime.log" >&2
      exit 2
    fi
  fi
  for obj in $RUNTIME; do
    if [ ! -f "$obj" ]; then
      echo "check-docs-examples: missing runtime object $obj" >&2
      exit 2
    fi
  done
fi

count=0
failures=0
while IFS=' ' read -r n name flags; do
  count=$((count + 1))
  src=$SCRATCH/$n.c
  bin=$SCRATCH/$n.bin
  expected=$SCRATCH/$n.expected
  actual=$SCRATCH/$n.out
  errlog=$SCRATCH/$n.err
  runtime=0
  case " $flags " in
    *" -fcrug-goroutines "*|*" -fcrug-channels "*) runtime=1 ;;
  esac

  if [ "$runtime" -eq 1 ]; then
    crugc -O2 $flags -I"$LIB/core" "$src" $RUNTIME -pthread -o "$bin" > "$SCRATCH/$n.compile" 2>&1
    status=$?
  else
    crugc -O2 $flags "$src" -o "$bin" > "$SCRATCH/$n.compile" 2>&1
    status=$?
  fi

  if [ "$status" -ne 0 ]; then
    echo "FAIL: $name (compile)" >&2
    cat "$SCRATCH/$n.compile" >&2
    failures=$((failures + 1))
    continue
  fi

  "$bin" > "$actual" 2> "$errlog"
  status=$?
  if [ "$status" -ne 0 ]; then
    echo "FAIL: $name (exit $status)" >&2
    cat "$errlog" >&2
    failures=$((failures + 1))
    continue
  fi

  if ! cmp -s "$expected" "$actual"; then
    echo "FAIL: $name (stdout mismatch)" >&2
    diff -u "$expected" "$actual" >&2 || true
    failures=$((failures + 1))
  fi
done < "$SCRATCH/examples.list"

printf 'docs-examples: %s example(s), %s failure(s)\n' "$count" "$failures"
if [ "$failures" -ne 0 ]; then
  exit 1
fi
exit 0
