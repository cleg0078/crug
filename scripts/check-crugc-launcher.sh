#!/bin/sh
# check-crugc-launcher.sh - verify how gcc/crugc resolves its GCC driver.
#
# The launcher is the only Crug entry point a user runs, and an installed
# package ships it behind /usr/bin/crugc.  These cases pin the resolution:
#
#   1. the in-tree launcher prints the seven-line version block;
#   2. the same launcher reached through a symlink still finds the checkout;
#   3. an installed-shaped tree (a symlink to <prefix>/bin/crugc that sits in
#      another directory) finds <prefix>/bin/gcc, reads the version from
#      <prefix>/share/doc/crug/VERSION and compiles a program;
#   4. a directory that is not a compiler installation refuses to fall back to
#      a gcc binary sitting next to the launcher (the /usr/bin/gcc trap);
#   5. an unusable CRUGC_REAL_DRIVER is reported instead of exec'd.
#
# Exit: 0 all cases pass, 1 a case failed.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
launcher="$root/gcc/crugc"
xgcc="$root/build/obj/gcc/xgcc"
tmp="$root/build/launcher-test"
version=$(cat "$root/VERSION")
fail=0

bad () {
  printf 'check-crugc-launcher: FAIL: %s\n' "$1"
  fail=$((fail + 1))
}

# case 1: in-tree version block.
if out=$("$launcher" --version 2>&1); then
  n=$(printf '%s\n' "$out" | wc -l | tr -d ' ')
  first=$(printf '%s\n' "$out" | sed -n '1p')
  [ "$n" -eq 7 ] || bad "in-tree --version printed $n line(s), want 7"
  [ "$first" = "crugc $version" ] || bad "in-tree --version first line is '$first'"
else
  bad "in-tree --version failed: $out"
fi

rm -rf "$tmp"
mkdir -p "$tmp"

# case 2: a symlink to the in-tree launcher, from an unrelated directory.
mkdir -p "$tmp/link"
ln -s "$launcher" "$tmp/link/crugc"
if out=$("$tmp/link/crugc" --version 2>&1); then
  n=$(printf '%s\n' "$out" | wc -l | tr -d ' ')
  [ "$n" -eq 7 ] || bad "symlinked in-tree --version printed $n line(s), want 7"
else
  bad "symlinked in-tree launcher failed: $out"
fi

# case 3: an installed-shaped tree, reached through a symlink.
[ -x "$xgcc" ] || { echo "check-crugc-launcher: no $xgcc (run make baseline)" >&2; exit 2; }
prefix="$tmp/prefix"
mkdir -p "$prefix/bin" "$prefix/libexec/gcc" "$prefix/share/doc/crug" "$tmp/usr/bin"
cp "$launcher" "$prefix/bin/crugc"
printf '#!/bin/sh\nexec "%s" -B"%s/" "$@"\n' "$xgcc" "$(dirname "$xgcc")" > "$prefix/bin/gcc"
chmod 755 "$prefix/bin/crugc" "$prefix/bin/gcc"
printf '9.9.9\n' > "$prefix/share/doc/crug/VERSION"
ln -s "$prefix/bin/crugc" "$tmp/usr/bin/crugc"
if out=$("$tmp/usr/bin/crugc" --version 2>&1); then
  first=$(printf '%s\n' "$out" | sed -n '1p')
  [ "$first" = "crugc 9.9.9" ] || bad "installed tree did not read share/doc/crug/VERSION: '$first'"
else
  bad "installed-shaped tree --version failed: $out"
fi
cat > "$tmp/hello.c" <<'EOF'
#include <stdio.h>
int main (void) { printf ("launcher-ok\n"); return 0; }
EOF
if "$tmp/usr/bin/crugc" "$tmp/hello.c" -o "$tmp/hello" 2>"$tmp/cc.err" &&
   "$tmp/hello" > "$tmp/hello.out" 2>&1 &&
   grep -qx 'launcher-ok' "$tmp/hello.out"; then
  :
else
  bad "installed-shaped tree did not compile and run a program: $(cat "$tmp/cc.err")"
fi

# case 4: a gcc binary next to the launcher that is not a Crug installation.
mkdir -p "$tmp/host/bin"
cp "$launcher" "$tmp/host/bin/crugc"
chmod 755 "$tmp/host/bin/crugc"
host_cc=$(command -v gcc 2>/dev/null || true)
[ -n "$host_cc" ] || host_cc=/bin/true
ln -s "$host_cc" "$tmp/host/bin/gcc"
if out=$("$tmp/host/bin/crugc" "$tmp/hello.c" -o "$tmp/host/hello" 2>&1); then
  bad "a launcher next to the host gcc selected it instead of failing"
else
  rc=$?
  [ "$rc" -eq 1 ] || bad "host-gcc fallback exited $rc, want 1"
  case "$out" in
    *"no Crug GCC driver found"*) ;;
    *) bad "host-gcc fallback message: $out" ;;
  esac
fi

# case 5: an unusable CRUGC_REAL_DRIVER.
if out=$(CRUGC_REAL_DRIVER=/nonexistent/crugc "$launcher" --version 2>&1); then
  bad "an unusable CRUGC_REAL_DRIVER was accepted"
else
  rc=$?
  [ "$rc" -eq 1 ] || bad "unusable CRUGC_REAL_DRIVER exited $rc, want 1"
  case "$out" in
    *"CRUGC_REAL_DRIVER is not executable"*) ;;
    *) bad "unusable CRUGC_REAL_DRIVER message: $out" ;;
  esac
fi

cases=5
if [ "$fail" -ne 0 ]; then
  printf 'check-crugc-launcher: %d case(s), %d failure(s)\n' "$cases" "$fail"
  exit 1
fi
printf 'check-crugc-launcher: %d case(s), 0 failure(s)\n' "$cases"
