#!/bin/sh
# check-crugc-resolution.sh - a check-* script must validate THIS checkout's
# compiler, not an installed crugc that happens to sit earlier on PATH (K-042).
#
# Two machine-checked rules over every script that resolves a crugc:
#   1. order: if a script probes PATH with `command -v crugc`, the in-tree
#      branch (gcc/crugc or a PATH prepend) must come first, so an installed
#      older release cannot win;
#   2. identity: with a fake crugc shim at the FRONT of PATH, each script must
#      still reach this checkout's compiler - asserted directly for the
#      scripts that honour an exported CRUGC, and by source order for the
#      rest (the prepend and the in-tree branch are checked under rule 1).
#
# Run from the repository root: scripts/check-crugc-resolution.sh
set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pass=0
fail=0

say () { printf '%s\n' "$*"; }
ok () { pass=$((pass+1)); say "ok: $*"; }
no () { fail=$((fail+1)); say "FAIL: $*"; }

scripts="
	check-borrow-abi
	check-borrow-cost
	check-borrow-dump
	check-borrow-escape
	check-borrow-nll
	check-borrow-policy
	check-borrow-real-c
	check-drop-asan
	check-drop-dump
	check-drop-exactly-once
	check-drop-objdump
	check-drop-policy
	check-ffi-codegen
	check-ffi-conflict
	check-ffi-integration
	check-ffi-policy
	check-kernel-freestanding
	check-owned-payloads
	check-safe-bounds
	check-strict-codegen
	check-strict-policy
	check-strict-raw
	test-kernel-compat
	check-docs-examples
"

for name in $scripts; do
  f="$root/scripts/$name.sh"
  [ -f "$f" ] || { no "$name.sh is missing"; continue; }
  # Locate first mention of each resolution strategy.
  tree=$(grep -n 'gcc/crugc\|PATH=.*\$root/gcc:\|PATH=.*\$ROOT/gcc:\|PATH=.*\$root/gcc ' "$f" | head -n1 | cut -d: -f1)
  path=$(grep -n 'command -v crugc' "$f" | head -n1 | cut -d: -f1)
  if [ -z "$path" ] && [ -z "$tree" ]; then
    no "$name.sh resolves neither gcc/crugc nor a PATH prepend"
    continue
  fi
  if [ -z "$path" ]; then
    ok "$name.sh never consults PATH for crugc"
    continue
  fi
  if [ -z "$tree" ]; then
    no "$name.sh probes PATH for crugc without preferring this checkout"
    continue
  fi
  if [ "$tree" -lt "$path" ]; then
    ok "$name.sh prefers this checkout before probing PATH"
  else
    no "$name.sh probes PATH (line $path) before the in-tree branch (line $tree)"
  fi
done

# Identity: an exported CRUGC must survive a hostile PATH.  check-docs-examples
# is the one script that honours CRUGC; prove the compiler it uses is this
# checkout's, even with a fake crugc in front.
if [ -x "$root/gcc/crugc" ]; then
  shimdir=$(mktemp -d)
  trap 'rm -rf "$shimdir"' EXIT INT TERM
  printf '#!/bin/sh\necho "shim crugc must not be used" >&2\nexit 77\n' > "$shimdir/crugc"
  chmod +x "$shimdir/crugc"
  out=$(CRUGC="$root/gcc/crugc" PATH="$shimdir:$PATH" sh "$root/scripts/check-docs-examples.sh" 2>&1)
  rc=$?
  if [ "$rc" -eq 0 ] && ! printf '%s' "$out" | grep -q 'shim crugc'; then
    ok "check-docs-examples.sh honours an exported CRUGC over a hostile PATH"
  else
    printf '%s\n' "$out" | head -n 5
    no "check-docs-examples.sh used PATH despite CRUGC=$root/gcc/crugc (exit $rc)"
  fi
else
  say "note: gcc/crugc not built; identity check needs the in-tree compiler"
fi

say "check-crugc-resolution: $pass check(s) passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
exit 0
