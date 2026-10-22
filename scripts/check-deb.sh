#!/bin/bash
# check-deb.sh - verify the Crug Debian binary package.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# The package is checked the way a user's machine would use it: the control
# fields, the payload layout, the md5sums, the dependencies recomputed from
# the payload, the extracted compiler's --version block, the three smoke
# programs, and the /usr/bin/crugc link that an installed system runs.
#
# Usage: scripts/check-deb.sh [DEB]
#
#   DEB   package to check (default: the newest dist/crug_<version>-*_amd64.deb)
#
# Exit status: 0 the package is good, 1 a check failed, 2 a prerequisite is
# missing.  Installation itself (apt-get install) is checked by
# scripts/check-packaging.sh in a ubuntu:24.04 container.
set -u
umask 022

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

say () { printf 'check-deb: %s\n' "$*"; }
problem () { printf 'check-deb: problem: %s\n' "$*"; problems=$((problems + 1)); }
die () { printf 'check-deb: %s\n' "$*" >&2; exit 2; }

for tool in dpkg-deb readelf md5sum find awk sed sort head wc; do
  command -v "$tool" >/dev/null 2>&1 || die "required tool '$tool' is not on PATH"
done

version=$(cat "$ROOT/VERSION") || die "cannot read VERSION"
problems=0
tmp=$ROOT/build/deb-check

if [ $# -gt 0 ]; then
  deb=$1
else
  deb=$(ls -1t "$ROOT"/dist/crug_"$version"-*_amd64.deb 2>/dev/null | head -n 1)
  [ -n "$deb" ] || die "no dist/crug_$version-*_amd64.deb (run 'make package-deb' first)"
fi
[ -f "$deb" ] || die "no such package: $deb"
say "checking $deb"

info=$(dpkg-deb --info "$deb" 2>/dev/null) || die "dpkg-deb cannot read the package"
field () { printf '%s\n' "$info" | sed -n "s/^ $1: //p" | head -n 1; }

# ---- the control fields ------------------------------------------------------
[ "$(field Package)" = crug ] || problem "Package is '$(field Package)', want 'crug'"
case $(field Version) in
  "$version"-[0-9]*) ;;
  *) problem "Version is '$(field Version)', want '$version-<revision>'" ;;
esac
[ "$(field Architecture)" = amd64 ] || problem "Architecture is '$(field Architecture)', want amd64"
[ "$(field Section)" = devel ] || problem "Section is '$(field Section)', want devel"
[ -n "$(field Maintainer)" ] || problem "no Maintainer"
case $(field Homepage) in
  *github.com/cleg0078/crug*) ;;
  *) problem "Homepage is '$(field Homepage)'" ;;
esac
esize=$(field Installed-Size)
case $esize in
  ''|*[!0-9]*) problem "Installed-Size is '$esize'" ;;
  *) [ "$esize" -gt 100000 ] || problem "Installed-Size $esize KiB is too small for a compiler" ;;
esac
[ -n "$(field Description)" ] || problem "no Description"

depends=$(field Depends)
for want in 'libc6 (>=' 'libgcc-s1' 'libstdc++6' 'libzstd1' 'binutils' 'libc6-dev'; do
  case $depends in
    *"$want"*) ;;
    *) problem "Depends lacks '$want': $depends" ;;
  esac
done

# ---- the payload layout ------------------------------------------------------
contents=$(dpkg-deb --contents "$deb" 2>/dev/null) || die "dpkg-deb cannot list the package"
paths=$(printf '%s\n' "$contents" | awk '{ print $6 }')

required='./opt/crug/bin/crugc
./opt/crug/lib/libcrug.a
./opt/crug/include/crug/task.h
./opt/crug/include/crug/chan.h
./opt/crug/include/crug/crug_pal.h
./opt/crug/share/doc/crug/LICENSE
./usr/bin/crugc
./usr/include/crug
./usr/lib/x86_64-linux-gnu/libcrug.a
./usr/share/doc/crug/copyright
./usr/share/doc/crug/changelog.Debian.gz'
for r in $required; do
  grep -qxF -- "$r" <<<"$paths" || problem "the package does not contain $r"
done

while IFS= read -r p; do
  [ -n "$p" ] || continue
  # dpkg-deb lists directories with a trailing slash and the root as "./".
  case $p in
    ./) p=. ;;
    */) p=${p%/} ;;
  esac
  case $p in
    .|./opt|./opt/*) ;;
    ./usr|./usr/bin|./usr/bin/crugc) ;;
    ./usr/include|./usr/include/crug) ;;
    ./usr/lib|./usr/lib/x86_64-linux-gnu|./usr/lib/x86_64-linux-gnu/libcrug.a) ;;
    ./usr/share|./usr/share/doc|./usr/share/doc/crug) ;;
    ./usr/share/doc/crug/copyright|./usr/share/doc/crug/changelog.Debian.gz) ;;
    *) problem "unexpected path $p" ;;
  esac
done < <(printf '%s\n' "$paths")

if grep -qE '^\./usr/bin/(gcc|x86_64-pc-linux-gnu-)' <<<"$paths"; then
  problem "the package installs a compiler into /usr/bin (it must not shadow the system gcc)"
fi

# dpkg stores root/root ownership because the package was built with
# --root-owner-group, and no file may be setuid, setgid, sticky or writable by
# group or others.
bad=$(printf '%s\n' "$contents" | awk '
  { owner = $2; mode = substr($1, 1, 10); path = $6
    if (owner != "root/root") print "owner " owner " " path
    # A symlink is stored as lrwxrwxrwx; its mode is not a permission.
    if (substr(mode, 1, 1) != "l" &&
        (substr(mode, 4, 1) ~ /[sS]/ || substr(mode, 7, 1) ~ /[sS]/ ||
         substr(mode, 10, 1) ~ /[tT]/ || substr(mode, 6, 1) == "w" ||
         substr(mode, 9, 1) == "w"))
      print "mode " mode " " path }')
if [ -n "$bad" ]; then
  problem "unsafe ownership or mode:"
  printf '%s\n' "$bad" | sed -n '1,5p' | while IFS= read -r l; do
    printf 'check-deb: problem:   %s\n' "$l"
  done
fi

# ---- the payload itself ------------------------------------------------------
rm -rf "$tmp" || die "cannot remove $tmp"
mkdir -p "$tmp" || die "cannot create $tmp"
dpkg-deb -e "$deb" "$tmp/control" 2>/dev/null || die "cannot extract the control directory"
dpkg-deb -x "$deb" "$tmp/root" 2>/dev/null || die "cannot extract the payload"

[ -s "$tmp/control/md5sums" ] || problem "no DEBIAN/md5sums in the package"
if [ -s "$tmp/control/md5sums" ]; then
  n=$(wc -l <"$tmp/control/md5sums")
  [ "$n" -gt 900 ] || problem "DEBIAN/md5sums lists only $n file(s)"
  if ! ( cd "$tmp/root" && md5sum -c --quiet "$tmp/control/md5sums" ) >"$tmp/md5.log" 2>&1; then
    problem "DEBIAN/md5sums does not match the payload: $(sed -n '1,3p' "$tmp/md5.log")"
  fi
fi

# The declared libc floor must be the floor the payload actually needs.
glibc_floor=$(find "$tmp/root/opt/crug" -type f -print0 | while IFS= read -r -d '' f; do
  readelf -h "$f" >/dev/null 2>&1 || continue
  readelf --version-info "$f" 2>/dev/null | sed -n 's/.*Name: \(GLIBC_[0-9.]*\).*/\1/p'
done | sed 's/^GLIBC_//' | LC_ALL=C sort -uV | tail -n 1)
declared=$(printf '%s\n' "$depends" | sed -n 's/^libc6 (>= \([0-9.]*\)).*/\1/p')
[ -n "$glibc_floor" ] || problem "cannot measure the GLIBC symbol floor from the payload"
if [ -n "$glibc_floor" ] && [ -n "$declared" ] && [ "$declared" != "$glibc_floor" ]; then
  problem "Depends says libc6 (>= $declared) but the payload needs GLIBC_$glibc_floor"
fi

needed=$(find "$tmp/root/opt/crug" -type f -print0 | while IFS= read -r -d '' f; do
  readelf -h "$f" >/dev/null 2>&1 || continue
  readelf -d "$f" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'
done | LC_ALL=C sort -u)
for soname in $needed; do
  case $soname in
    libzstd.so.1) pkg=libzstd1 ;;
    libgcc_s.so.1) pkg=libgcc-s1 ;;
    libstdc++.so.6) pkg=libstdc++6 ;;
    libc.so.6|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1|ld-linux-x86-64.so.2) pkg=libc6 ;;
    *) pkg= ;;
  esac
  if [ -n "$pkg" ]; then
    case $depends in
      *"$pkg"*) ;;
      *) problem "the payload needs $soname but Depends has no $pkg" ;;
    esac
  fi
done

# ---- the compiler in the package --------------------------------------------
prefix=$tmp/root/opt/crug
out=$("$prefix/bin/crugc" --version 2>&1) || problem "the packaged crugc --version failed: $out"
n=$(printf '%s\n' "$out" | wc -l)
[ "$n" -eq 7 ] || problem "the packaged crugc --version printed $n line(s), want 7"
[ "$(printf '%s\n' "$out" | sed -n 1p)" = "crugc $version" ] \
  || problem "the packaged crugc reports '$(printf '%s\n' "$out" | sed -n 1p)', want 'crugc $version'"
printf '%s\n' "$out" | grep -q '^compiler: GCC ' || problem "the packaged crugc reports no compiler version"

search=$("$prefix/bin/gcc" -print-search-dirs 2>/dev/null)
case $search in
  *"$prefix"*) ;;
  *) problem "the packaged driver does not look inside the installed tree" ;;
esac
specs=$("$prefix/bin/gcc" -dumpspecs 2>/dev/null)
case $specs in
  *"$ROOT"*) problem "the packaged driver embeds the build directory $ROOT" ;;
esac

mkdir -p "$tmp/src"
cp "$ROOT"/packaging/smoke/*.c "$tmp/src/" || die "cannot copy packaging/smoke"

compile () {
  name=$1
  shift
  if "$@" >"$tmp/$name.cc.log" 2>&1; then
    say "compiled $name.c"
    return 0
  fi
  problem "compiling $name.c failed: $(tail -n 3 "$tmp/$name.cc.log" | tr '\n' ' ')"
  return 1
}
run_bin () {
  name=$1
  expect=$2
  shift 2
  if "$@" >"$tmp/$name.run.log" 2>&1 && grep -qx "$expect" "$tmp/$name.run.log"; then
    say "smoke $name: ok"
  else
    problem "smoke $name failed: $(tail -n 3 "$tmp/$name.run.log" | tr '\n' ' ')"
  fi
}

if compile hello "$prefix/bin/crugc" -O2 "$tmp/src/hello.c" -o "$tmp/hello"; then
  run_bin hello 'hello from crug' "$tmp/hello"
fi
if compile hello-own "$prefix/bin/crugc" -O2 -fcrug-ownership \
     "$tmp/src/hello-own.c" -o "$tmp/hello-own"; then
  run_bin hello-own 'own-ok value=42' "$tmp/hello-own"
fi
if compile runtime "$prefix/bin/crugc" -O2 -fcrug-goroutines -fcrug-channels \
     -I"$prefix/include/crug" -L"$prefix/lib" "$tmp/src/runtime-sample.c" \
     -lcrug -pthread -o "$tmp/runtime-sample"; then
  run_bin runtime 'runtime-ok sum=7' "$tmp/runtime-sample"
fi

# ---- the installed entry point ----------------------------------------------
# The package ships an absolute link to /opt/crug; in the extraction that path
# does not exist, so replace it with the relative link an installed system
# would resolve and run the launcher through it.  This is the case that
# silently selected the host gcc before the launcher followed symlinks.
ln -sf ../../opt/crug/bin/crugc "$tmp/root/usr/bin/crugc" \
  || problem "cannot relink $tmp/root/usr/bin/crugc"
out=$("$tmp/root/usr/bin/crugc" --version 2>&1)
if [ "$(printf '%s\n' "$out" | sed -n 1p)" = "crugc $version" ]; then
  say "installed entry point: /usr/bin/crugc -> /opt/crug/bin/crugc"
else
  problem "/usr/bin/crugc did not run the packaged crugc: $(printf '%s\n' "$out" | sed -n '1,3p')"
fi

# ---- the result --------------------------------------------------------------
if [ "$problems" -ne 0 ]; then
  printf 'check-deb: %d problem(s)\n' "$problems"
  exit 1
fi
say "0 problem(s)"
say "OK"
exit 0
