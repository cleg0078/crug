#!/bin/sh
# package.sh - build the Phase 24 release artefacts.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# Installs the compiler built in build/obj into a private stage with DESTDIR
# (the build is configured --prefix=/opt/crug, so the tree lands under
# <stage>/opt/crug), adds the Crug-specific pieces the GCC install does not
# know about, prunes the C++ front end, strips the compiler, tars the tree
# deterministically, and writes the SPDX SBOM and the checksums next to it:
#
#   <out>/crug-<version>-linux-x86_64.tar.gz   the install tree (prefix crug-<version>/)
#   <out>/crug-<version>.spdx.json             SPDX 2.3 SBOM (scripts/make-sbom.py)
#   <out>/LICENSE, <out>/NOTICE                the licence texts
#   <out>/SHA256SUMS                           checksums of the four artefacts
#
# The tar is deterministic by construction: sorted names, zeroed mtimes and
# ownership, and gzip -n.
#
# Usage: scripts/package.sh [--version V] [--out DIR] [--stage DIR] [--skip-build]
#
#   --version V   release version (default: the contents of VERSION)
#   --out DIR     artefact directory (default: dist/)
#   --stage DIR   install root passed to DESTDIR (default: build/package/crug-<version>)
#   --skip-build  fail instead of building a missing compiler, libgcc, libatomic
#                 or libcrug
#
# Exit status: 0 packaged, 1 the packaging failed, 2 a prerequisite is missing.
# Verify the result with scripts/check-release.sh.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

version=
out=
stage=
skip_build=0
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

say () { printf 'package: %s\n' "$*"; }
warn () { printf 'package: %s\n' "$*" >&2; }
die () { printf 'package: %s\n' "$*" >&2; exit 2; }
fail () { printf 'package: %s\n' "$*" >&2; exit 1; }

usage () {
  sed -n '2,/^set -u$/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
  case $1 in
    --version)
      [ $# -ge 2 ] || fail 'option --version needs a value'
      version=$2
      shift 2
      ;;
    --out)
      [ $# -ge 2 ] || fail 'option --out needs a directory'
      out=$2
      shift 2
      ;;
    --stage)
      [ $# -ge 2 ] || fail 'option --stage needs a directory'
      stage=$2
      shift 2
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown argument: $1 (try --help)"
      ;;
  esac
done

[ -n "$version" ] || version=$(cat "$ROOT/VERSION" 2>/dev/null) || die "no VERSION file and no --version"
[ -n "$version" ] || die "empty release version"
[ -n "$out" ] || out=$ROOT/dist
[ -n "$stage" ] || stage=$ROOT/build/package/crug-$version

case $version in
  *[!0-9A-Za-z._-]*)
    die "not a publishable version: '$version'"
    ;;
esac
case $stage in
  ""|/|.) die "refusing to use '$stage' as the install stage" ;;
esac
case $out in
  ""|/|.) die "refusing to use '$out' as the output directory" ;;
esac
# libtool's install step rejects a relative DESTDIR, so the caller's relative
# --stage/--out are anchored to the invocation directory here.
case $stage in
  /*) ;;
  *) stage=$PWD/$stage ;;
esac
case $out in
  /*) ;;
  *) out=$PWD/$out ;;
esac

for tool in make tar gzip sha256sum python3 ar cp chmod mkdir rm sed awk wc tr getconf strip find; do
  command -v "$tool" >/dev/null 2>&1 || die "required tool '$tool' is not on PATH"
done

[ "$(uname -s 2>/dev/null)" = Linux ] && [ "$(uname -m 2>/dev/null)" = x86_64 ] \
  || die "the release artefacts are built on Linux x86-64, not $(uname -s 2>/dev/null)/$(uname -m 2>/dev/null)"

log=$ROOT/build/package/package.log
mkdir -p "$ROOT/build/package" || fail "cannot create build/package"

# ---- the compiler -----------------------------------------------------------
xgcc=$ROOT/build/obj/gcc/xgcc
if [ ! -x "$xgcc" ]; then
  if [ "$skip_build" -eq 1 ]; then
    die "no in-tree compiler at $xgcc (build it with 'make baseline JOBS=$jobs', or drop --skip-build)"
  fi
  say "building the pinned GCC baseline (make baseline JOBS=$jobs)"
  ( cd "$ROOT" && make baseline JOBS="$jobs" ) >"$log" 2>&1 \
    || { tail -n 20 "$log" >&2; die "make baseline failed; see $log"; }
fi
[ -x "$xgcc" ] || die "no in-tree compiler at $xgcc after the build"

# ---- the host and target directories make install needs ---------------------
# "make baseline" stops at all-gcc. The full install also walks the host
# directories (c++tools, lto-plugin, fixincludes) and the target libraries, so
# build those here unless the caller asserted the tree is complete. This is a
# no-op on a tree that has already been built with "make" or "make all".
if [ "$skip_build" -eq 0 ]; then
  say "building the host and target directories the install walks (all-target all-host)"
  ( cd "$ROOT/build/obj" && make -j "$jobs" all-target all-host ) >>"$log" 2>&1 \
    || { tail -n 20 "$log" >&2; die "building all-target/all-host failed; see $log"; }
fi

# ---- the target libraries the driver needs ----------------------------------
have_libgcc=0
for candidate in "$ROOT"/build/obj/*/libgcc/libgcc.a "$ROOT/build/obj/gcc/libgcc.a"; do
  [ -f "$candidate" ] && have_libgcc=1
done
have_libatomic=0
for candidate in "$ROOT"/build/obj/*/libatomic/.libs/libatomic.a "$ROOT/build/obj/gcc/libatomic.a"; do
  [ -f "$candidate" ] && have_libatomic=1
done
missing_libs=
[ "$have_libgcc" -eq 1 ] || missing_libs="$missing_libs libgcc"
[ "$have_libatomic" -eq 1 ] || missing_libs="$missing_libs libatomic"
if [ -n "$missing_libs" ]; then
  if [ "$skip_build" -eq 1 ]; then
    die "missing target libraries:$missing_libs (build with 'make -C build/obj all-target-libgcc all-target-libatomic', or drop --skip-build)"
  fi
  say "building the target libraries (all-target-libgcc all-target-libatomic)"
  ( cd "$ROOT/build/obj" && make -j "$jobs" all-target-libgcc all-target-libatomic ) >>"$log" 2>&1 \
    || { tail -n 20 "$log" >&2; die "building libgcc/libatomic failed; see $log"; }
fi

# ---- the Crug runtime --------------------------------------------------------
say "building the Crug runtime (make -C libcrug PAL=linux)"
( cd "$ROOT" && make -C libcrug PAL=linux ) >>"$log" 2>&1 \
  || { tail -n 20 "$log" >&2; die "building libcrug failed; see $log"; }
runtime_objs='task.o pal_linux.o switch.o stack.o chan.o'
for obj in $runtime_objs; do
  [ -f "$ROOT/libcrug/build/linux/$obj" ] || die "missing libcrug object libcrug/build/linux/$obj"
done

# ---- stage the install tree --------------------------------------------------
say "installing the compiler into $stage/opt/crug (DESTDIR)"
rm -rf "$stage" || fail "cannot remove the old stage $stage"
mkdir -p "$stage/opt/crug" || fail "cannot create the stage $stage/opt/crug"
( cd "$ROOT/build/obj" && make DESTDIR="$stage" install ) >>"$log" 2>&1 \
  || { tail -n 20 "$log" >&2; die "make install failed; see $log"; }
prefix=$stage/opt/crug
[ -x "$prefix/bin/x86_64-pc-linux-gnu-gcc-16.2.0" ] \
  || die "the install produced no x86_64-pc-linux-gnu-gcc-16.2.0 under $prefix/bin"

cp "$ROOT/gcc/crugc" "$prefix/bin/crugc" || fail "cannot install bin/crugc"
chmod 0755 "$prefix/bin/crugc"

mkdir -p "$prefix/include/crug" "$prefix/lib" "$prefix/share/doc/crug" || fail "cannot create the Crug directories"
for header in task.h chan.h; do
  cp "$ROOT/libcrug/core/$header" "$prefix/include/crug/$header" || fail "cannot install include/crug/$header"
  chmod 0644 "$prefix/include/crug/$header"
done
cp "$ROOT/libcrug/pal/include/crug_pal.h" "$prefix/include/crug/crug_pal.h" \
  || fail "cannot install include/crug/crug_pal.h"
chmod 0644 "$prefix/include/crug/crug_pal.h"

( cd "$ROOT/libcrug/build/linux" && ar rcs "$prefix/lib/libcrug.a" $runtime_objs ) \
  || fail "cannot archive lib/libcrug.a"
chmod 0644 "$prefix/lib/libcrug.a"

for doc in LICENSE NOTICE CHANGELOG.md; do
  cp "$ROOT/$doc" "$prefix/share/doc/crug/$doc" || fail "cannot install share/doc/crug/$doc"
  chmod 0644 "$prefix/share/doc/crug/$doc"
done
cp "$ROOT/VERSION" "$prefix/share/doc/crug/VERSION" || fail "cannot install share/doc/crug/VERSION"
chmod 0644 "$prefix/share/doc/crug/VERSION"

# ---- prune and strip ---------------------------------------------------------
# The fork is configured with c++, but the Crug release is a C compiler: drop
# the C++ front end and its driver aliases, then strip the remaining
# executables so the tarball stays small. Every path is guarded, so a build
# configured without c++ still packages.
say "pruning the C++ front end and stripping the compiler"
for f in bin/c++ bin/g++ bin/x86_64-pc-linux-gnu-c++ \
         bin/x86_64-pc-linux-gnu-g++ bin/x86_64-pc-linux-gnu-g++-16.2.0 \
         bin/g++-mapper-server; do
  [ -e "$prefix/$f" ] && rm -f "$prefix/$f"
done
lexec=$prefix/libexec/gcc/x86_64-pc-linux-gnu/16.2.0
for f in "$lexec/cc1plus" "$lexec/g++-mapper-server"; do
  [ -e "$f" ] && rm -f "$f"
done
[ -e "$prefix/include/c++" ] && rm -rf "$prefix/include/c++"

cc1=$lexec/cc1
cc1_before=$(wc -c <"$cc1" 2>/dev/null | tr -d ' ')
[ -n "$cc1_before" ] || cc1_before=0
for dir in "$prefix/bin" "$prefix/libexec/gcc"; do
  [ -d "$dir" ] || continue
  find "$dir" -type f ! -name '*.so' ! -name '*.so.*' 2>/dev/null | while IFS= read -r f; do
    strip --strip-all "$f" 2>/dev/null || true
  done
done
cc1_after=$(wc -c <"$cc1" 2>/dev/null | tr -d ' ')
[ -n "$cc1_after" ] || cc1_after=0
say "pruned and stripped (cc1 $cc1_before -> $cc1_after bytes)"

# ---- the tarball -------------------------------------------------------------
mkdir -p "$out" || fail "cannot create the output directory $out"
tarball=crug-$version-linux-x86_64.tar.gz
flat=$stage/package.tar
say "writing $out/$tarball (deterministic tar, gzip -n)"
( cd "$stage/opt" && LC_ALL=C tar \
    --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
    --transform="s,^crug,crug-$version," \
    -cf "$flat" crug ) || { rm -f "$flat"; fail "tar failed"; }
gzip -n -6 -c "$flat" >"$out/.$tarball.tmp.$$" || { rm -f "$flat" "$out/.$tarball.tmp.$$"; fail "gzip failed"; }
rm -f "$flat"
mv "$out/.$tarball.tmp.$$" "$out/$tarball" || fail "cannot move the tarball into place"
chmod 0644 "$out/$tarball"

bytes=$(wc -c <"$out/$tarball" | tr -d ' ')
files=$(tar -tzf "$out/$tarball" | awk '!/\/$/ { n++ } END { print n + 0 }')

# ---- the SBOM ----------------------------------------------------------------
gcc_version=$(sh "$ROOT/gcc/crugc" --version 2>/dev/null | sed -n 's/^compiler: GCC //p')
[ -n "$gcc_version" ] || gcc_version=16.2.0
say "writing the SPDX SBOM $out/crug-$version.spdx.json"
python3 "$ROOT/scripts/make-sbom.py" \
  --version "$version" \
  --gcc-version "$gcc_version" \
  --stage "$prefix" \
  --tarball "$out/$tarball" \
  --out "$out/crug-$version.spdx.json" \
  || fail "writing the SPDX SBOM failed"
chmod 0644 "$out/crug-$version.spdx.json"

# ---- the licence copies and the checksums ------------------------------------
install -m 0644 "$ROOT/LICENSE" "$out/LICENSE" || fail "cannot install LICENSE into $out"
install -m 0644 "$ROOT/NOTICE" "$out/NOTICE" || fail "cannot install NOTICE into $out"
( cd "$out" && sha256sum "$tarball" "crug-$version.spdx.json" LICENSE NOTICE >SHA256SUMS ) \
  || fail "cannot write SHA256SUMS"
chmod 0644 "$out/SHA256SUMS"

say "$tarball $bytes bytes, $files files"
say "dist/crug-$version.spdx.json"
say "SHA256SUMS: 4 artefact(s)"
exit 0
