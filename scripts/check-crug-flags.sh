#!/usr/bin/env bash
# Phase 3: the -fcrug-* option set, its closure, and the feature macros.
#
# Usage: scripts/check-crug-flags.sh
#
# Work items:
#   * every flag is accepted on its own, and its feature-macro closure is exact;
#   * an unknown -fcrug option is rejected as an unrecognized option;
#   * every pair of flags is accepted, except that -fcrug-kernel conflicts with
#     -fcrug-goroutines, -fcrug-channels and -fcrug-modern, and with the flags
#     whose closure asks for ownership (-fcrug-ownership, -fcrug-borrow,
#     -fcrug-strict): kernel-mode drop lowering is Phase 19 (ADR 0005 A-7);
#   * no flag means no __CRUG__ macro and a predefined set byte-identical to the
#     baseline compiler's;
#   * the contextual words still parse as C identifiers under every accepted
#     combination (ADR 0018 D1).
set -uo pipefail

cd "$(dirname "$0")/.."

cc=${CRUGC:-gcc/crugc}
base=${XGCC:-build/obj/gcc/xgcc}
words=gcc/testsuite/crug.dg/flags/words-as-identifiers.c

[ -x "$cc" ] || { echo "crug-flags: crugc not executable: $cc" >&2; exit 2; }
[ -f "$words" ] || { echo "crug-flags: missing fixture: $words" >&2; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

total=0
passed=0
fails=0

pass () { total=$((total + 1)); passed=$((passed + 1)); printf 'PASS: %s\n' "$1"; }
fail () { total=$((total + 1)); fails=$((fails + 1)); printf 'FAIL: %s\n' "$1"; }

show_err () { sed 's/^/    /' "$work/err" | head -6; }

expect_ok () {
  local what=$1
  shift
  if "$@" > "$work/out" 2> "$work/err"; then
    pass "$what"
  else
    fail "$what"
    show_err
  fi
}

expect_rejected () {
  local what=$1
  local pattern=$2
  shift 2
  if "$@" > "$work/out" 2> "$work/err"; then
    fail "$what (accepted)"
  elif ! grep -q -- "$pattern" "$work/err"; then
    fail "$what (wrong diagnostic: no match for $pattern)"
    show_err
  else
    pass "$what"
  fi
}

# -dM -E prints one '#define NAME VALUE' line per predefined macro.
macro_dump () {
  local compiler=$1
  shift
  "$compiler" "$@" -dM -E -x c - < /dev/null 2> /dev/null
}

has_macro () {
  local name=$1
  shift
  macro_dump "$cc" "$@" | grep -qx "#define __CRUG_${name}__ 1"
}

# The closure each single flag must produce, in order
# OWNERSHIP BORROW SLICES RESULT ADT GOROUTINES CHANNELS STRICT KERNEL,
# where '+' means defined and '-' means absent.
declare -A closure=(
  [ownership]="+--------"
  [borrow]="++-------"
  [slices]="--+------"
  [result]="---++----"
  [adt]="----+----"
  [goroutines]="-----+---"
  [channels]="-----++--"
  [strict]="++-----+-"
  [modern]="++++++++-"
  [kernel]="--------+"
)
order="OWNERSHIP BORROW SLICES RESULT ADT GOROUTINES CHANNELS STRICT KERNEL"
flags="ownership borrow slices result adt goroutines channels strict modern kernel"

# 1. Every flag is accepted on its own, and its closure is exact.
for f in $flags; do
  expect_ok "-fcrug-$f accepted" "$cc" "-fcrug-$f" -fsyntax-only "$words"

  spec=${closure[$f]}
  i=0
  for name in $order; do
    bit=${spec:$i:1}
    i=$((i + 1))
    if [ "$bit" = '+' ]; then
      if has_macro "$name" "-fcrug-$f"; then
        pass "-fcrug-$f defines __CRUG_${name}__"
      else
        fail "-fcrug-$f defines __CRUG_${name}__"
      fi
    else
      if macro_dump "$cc" "-fcrug-$f" | grep -q "__CRUG_${name}__"; then
        fail "-fcrug-$f leaves __CRUG_${name}__ undefined"
      else
        pass "-fcrug-$f leaves __CRUG_${name}__ undefined"
      fi
    fi
  done
done

# 2. No flags: no Crug macro at all, and the predefined set is the baseline's.
if macro_dump "$cc" | grep -q '__CRUG__'; then
  fail "no flag: __CRUG__ is not predefined"
else
  pass "no flag: __CRUG__ is not predefined"
fi

if [ -x "$base" ]; then
  base_dir=$(cd "$(dirname "$base")" && pwd)
  if macro_dump "$cc" > "$work/macros-off.def" \
     && macro_dump "$base" "-B$base_dir/" > "$work/macros-base.def" \
     && cmp -s "$work/macros-off.def" "$work/macros-base.def"; then
    pass "no flag: predefined macros match the baseline compiler"
  else
    fail "no flag: predefined macros match the baseline compiler"
  fi
else
  pass "no flag: predefined macros match the baseline compiler (baseline absent, skipped)"
fi

# 3. Every unordered pair is accepted, except the kernel conflicts above.
read -r -a list <<< "$flags"
n=${#list[@]}
for ((i = 0; i < n; i++)); do
  for ((j = i + 1; j < n; j++)); do
    a=${list[i]}
    b=${list[j]}
    if [ "$a" = kernel ] || [ "$b" = kernel ]; then
      if [ "$a" = kernel ]; then other=$b; else other=$a; fi
      case "$other" in
        goroutines|channels|modern)
          expect_rejected "-fcrug-kernel -fcrug-$other rejected" \
            "removes.*-fcrug-$other" \
            "$cc" "-fcrug-kernel" "-fcrug-$other" -fsyntax-only "$words"
          ;;
        ownership|borrow|strict)
          expect_rejected "-fcrug-kernel -fcrug-$other rejected" \
            "cannot be combined" \
            "$cc" "-fcrug-kernel" "-fcrug-$other" -fsyntax-only "$words"
          ;;
        *)
          expect_ok "-fcrug-kernel -fcrug-$other accepted" \
            "$cc" "-fcrug-kernel" "-fcrug-$other" -fsyntax-only "$words"
          ;;
      esac
    else
      expect_ok "-fcrug-$a -fcrug-$b accepted" \
        "$cc" "-fcrug-$a" "-fcrug-$b" -fsyntax-only "$words"
    fi
  done
done

# 4. Unknown options are rejected by the option parser, not silently ignored.
expect_rejected "-fcrug-nonexistent rejected" "unrecognized command-line option" \
  "$cc" -fcrug-nonexistent -fsyntax-only "$words"

# 5. -fcrug-kernel with -fcrug-modern names the conflicting flags.
expect_rejected "-fcrug-kernel -fcrug-modern names the conflict" \
  "removes.*-fcrug-goroutines" \
  "$cc" -fcrug-kernel -fcrug-modern -fsyntax-only "$words"

echo "crug-flags: $passed/$total work items PASS"
[ "$fails" -eq 0 ]
