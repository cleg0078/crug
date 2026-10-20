#!/bin/sh
# Build the unmodified GCC 16.2 compiler used by the Phase 21 codegen parity
# check (bench/run.py --pristine).
#
#   scripts/build-pristine.sh
#
# The baseline tarball is extracted to build/gcc-16.2.0 and configured in
# build/pristine-obj.  Unlike build/obj, that tree contains no Crug changes, so
# comparing its objects with gcc/crugc is a real test of "plain C is unchanged".
#
# Exits 0 when build/pristine-obj/gcc/xgcc exists and reports 16.2.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$ROOT/build
SRC=$BUILD/gcc-16.2.0
OBJ=$BUILD/pristine-obj
TARBALL=$ROOT/sources/gcc-16.2.0.tar.xz
STAMP=$BUILD/.stamps/pristine-xgcc
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

say() { printf 'pristine: %s\n' "$*"; }
die() { printf 'pristine: %s\n' "$*" >&2; exit 2; }

if [ -x "$OBJ/gcc/xgcc" ]; then
  version=$("$OBJ/gcc/xgcc" -B"$OBJ/gcc/" -dumpfullversion -dumpversion 2>/dev/null || echo unknown)
  say "compiler already built: $OBJ/gcc/xgcc ($version)"
  exit 0
fi

if [ ! -x "$SRC/configure" ]; then
  [ -f "$TARBALL" ] || die "no $TARBALL; run make download verify first"
  say "extracting $TARBALL into $BUILD"
  mkdir -p "$BUILD"
  tar --extract --xz --file="$TARBALL" --directory="$BUILD"
fi

# A pristine tree must not contain the fork's modules.
for marker in gcc/c/crug-ownership.cc gcc/c-family/crug-diagnostics.cc; do
  [ -f "$SRC/$marker" ] && die "$SRC/$marker exists; $SRC is the fork tree, not the tarball"
done

if [ ! -d "$SRC/gmp" ] || [ ! -d "$SRC/mpfr" ]; then
  say "fetching GCC build prerequisites into $SRC"
  ( cd "$SRC" && ./contrib/download_prerequisites ) || die "contrib/download_prerequisites failed"
fi

mkdir -p "$OBJ" "$BUILD/.stamps"
if [ ! -f "$OBJ/Makefile" ]; then
  say "configuring in $OBJ"
  ( cd "$OBJ" && "$SRC/configure" \
      --prefix=/opt/crug \
      --enable-languages=c,c++ \
      --disable-bootstrap \
      --disable-multilib \
      --enable-checking=yes \
      --with-arch=x86-64 --with-tune=generic )
fi

say "building all-gcc with $JOBS job(s); this takes a few minutes"
( cd "$OBJ" && make -j"$JOBS" all-gcc )

# The parity check also links LTO executables, which needs libgcc and libatomic.
say "building the target libraries needed to link (libgcc, libatomic)"
( cd "$OBJ" && make -j"$JOBS" all-target-libgcc all-target-libatomic )

[ -x "$OBJ/gcc/xgcc" ] || die "no xgcc after the build"
version=$("$OBJ/gcc/xgcc" -B"$OBJ/gcc/" -dumpfullversion -dumpversion)
case $version in
  16.2*) ;;
  *) die "unexpected pristine compiler version: $version" ;;
esac
touch "$STAMP"
say "built GCC $version in $OBJ"
