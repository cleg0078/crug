#!/bin/bash
# build-deb.sh - build the Crug Debian binary package.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# The package is assembled from the install tree scripts/package.sh stages
# under <stage>/opt/crug, so the tarball, the container image and the .deb all
# ship one compiler.  The tree stays in /opt/crug, which keeps every GCC
# internal path out of /usr; the package adds the system entry points:
#
#   /usr/bin/crugc                              -> /opt/crug/bin/crugc
#   /usr/include/crug                           -> /opt/crug/include/crug
#   /usr/lib/x86_64-linux-gnu/libcrug.a         -> /opt/crug/lib/libcrug.a
#   /usr/share/doc/crug/copyright
#   /usr/share/doc/crug/changelog.Debian.gz
#
# Nothing is installed where the system toolchain lives, so the package cannot
# shadow /usr/bin/gcc.  There is no maintainer script: the package only adds
# files and links, so it needs no postinst, prerm or postrm.
#
# Usage: scripts/build-deb.sh [--version V] [--revision R] [--out DIR]
#                             [--stage DIR] [--reuse-stage]
#
#   --version V    release version (default: the contents of VERSION)
#   --revision R   Debian revision (default: 1)
#   --out DIR      artefact directory (default: dist/)
#   --stage DIR    install root from DESTDIR (default: build/package/crug-<version>)
#   --reuse-stage  assemble from --stage without running scripts/package.sh
#                  (scripts/package.sh passes this after it staged the tree)
#
# The control file is rendered from packaging/debian/control.in; Depends is
# measured from the payload (the NEEDED libraries of every ELF file and the
# highest GLIBC_ symbol version) plus the compiler's own needs, binutils and
# libc6-dev.  md5sums and changelog.Debian are generated here; dpkg-deb does
# not write md5sums for a package built without debhelper.  The Maintainer
# field comes from DEBFULLNAME and DEBEMAIL when both are set (setting one is
# an error) and otherwise from a fixed release identity -- never from the
# build host's git author.
#
# The deb is not claimed to be byte-reproducible: dpkg-deb ignores
# SOURCE_DATE_EPOCH, so only the payload mtimes are normalised.
#
# Exit status: 0 packaged, 1 the packaging failed, 2 a prerequisite is missing.
# Verify the result with scripts/check-deb.sh.

set -u
umask 022

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

version=
revision=1
out=
stage=
reuse_stage=0

say () { printf 'deb: %s
' "$*"; }
warn () { printf 'deb: %s
' "$*" >&2; }
die () { printf 'deb: %s
' "$*" >&2; exit 2; }
fail () { printf 'deb: %s
' "$*" >&2; exit 1; }

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
    --revision)
      [ $# -ge 2 ] || fail 'option --revision needs a value'
      revision=$2
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
    --reuse-stage)
      reuse_stage=1
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
case $version in
  *[!0-9A-Za-z._-]*) die "not a publishable version: '$version'" ;;
esac
case $revision in
  *[!0-9A-Za-z.+~]*) die "not a Debian revision: '$revision'" ;;
esac
[ -n "$out" ] || out=$ROOT/dist
[ -n "$stage" ] || stage=$ROOT/build/package/crug-$version
case $stage in
  ""|/|.) die "refusing to use '$stage' as the install stage" ;;
esac
case $out in
  ""|/|.) die "refusing to use '$out' as the output directory" ;;
esac
case $stage in /*) ;; *) stage=$PWD/$stage ;; esac
case $out in /*) ;; *) out=$PWD/$out ;; esac

for tool in dpkg-deb find readelf md5sum sort awk du gzip sed tr wc xargs touch install cp ln rm mkdir; do
  command -v "$tool" >/dev/null 2>&1 || die "required tool '$tool' is not on PATH"
done
[ "$(uname -s 2>/dev/null)" = Linux ] && [ "$(uname -m 2>/dev/null)" = x86_64 ] \
  || die "the Debian package is built on Linux x86-64, not $(uname -s 2>/dev/null)/$(uname -m 2>/dev/null)"
arch=amd64

if [ "$reuse_stage" -eq 0 ]; then
  [ -x "$ROOT/scripts/package.sh" ] || die "no scripts/package.sh"
  say "running scripts/package.sh so the package is not stale"
  CRUG_DEB=skip sh "$ROOT/scripts/package.sh" --version "$version" --out "$out" \
    --stage "$stage" \
    || fail "scripts/package.sh failed (a $version package cannot be built)"
fi

prefix=$stage/opt/crug
[ -d "$prefix" ] || die "no install tree at $prefix (run 'make package' or scripts/package.sh first)"
[ -x "$prefix/bin/crugc" ] || die "the install tree has no bin/crugc at $prefix/bin/crugc"
[ -f "$prefix/lib/libcrug.a" ] || die "the install tree has no lib/libcrug.a at $prefix/lib/libcrug.a"
[ -d "$prefix/include/crug" ] || die "the install tree has no include/crug at $prefix/include/crug"

# ---- the payload dependencies ------------------------------------------------
say "measuring the payload dependencies"
needed=$(find "$prefix" -type f -print0 | while IFS= read -r -d '' f; do
  readelf -h "$f" >/dev/null 2>&1 || continue
  readelf -d "$f" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'
done | LC_ALL=C sort -u)
glibc_versions=$(find "$prefix" -type f -print0 | while IFS= read -r -d '' f; do
  readelf -h "$f" >/dev/null 2>&1 || continue
  readelf --version-info "$f" 2>/dev/null | sed -n 's/.*Name: \(GLIBC_[0-9.]*\).*/\1/p'
done | LC_ALL=C sort -u)
glibc_floor=$(printf '%s\n' "$glibc_versions" | sed -n 's/^GLIBC_//p' | sort -uV | tail -n 1)
[ -n "$glibc_floor" ] || die "cannot measure the GLIBC symbol floor from the payload"

depends=
add_dep () {
  case " $depends " in
    *" $1 "*) ;;
    *) depends=${depends:+$depends, }$1 ;;
  esac
}
need_libc6=0
for soname in $needed; do
  case $soname in
    libc.so.6|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1|ld-linux-x86-64.so.2)
      need_libc6=1
      ;;
    libgcc_s.so.1) add_dep libgcc-s1 ;;
    libstdc++.so.6) add_dep libstdc++6 ;;
    libzstd.so.1) add_dep libzstd1 ;;
    libz.so.1) add_dep zlib1g ;;
    libgomp.so.1) add_dep libgomp1 ;;
    libatomic.so.1) add_dep libatomic1 ;;
    *) warn "no Debian package known for $soname (not declared in Depends)" ;;
  esac
done
[ "$need_libc6" -eq 1 ] || warn "the payload needs no libc, which is unexpected"
# A compiler also needs binutils (as, ld, ar) and the C library headers.
add_dep binutils
add_dep libc6-dev
depends="libc6 (>= $glibc_floor)${depends:+, $depends}"

# ---- the package tree --------------------------------------------------------
pkgroot=$ROOT/build/package/deb-$version-$revision
say "assembling $pkgroot"
rm -rf "$pkgroot" || fail "cannot remove the old package tree $pkgroot"
mkdir -p "$pkgroot/DEBIAN" "$pkgroot/usr/bin" "$pkgroot/usr/include" \
         "$pkgroot/usr/lib/x86_64-linux-gnu" "$pkgroot/usr/share/doc/crug" \
  || fail "cannot create the package tree"
cp -a "$stage/opt" "$pkgroot/opt" || fail "cannot copy the install tree into the package"
ln -s /opt/crug/bin/crugc "$pkgroot/usr/bin/crugc" || fail "cannot link /usr/bin/crugc"
ln -s /opt/crug/include/crug "$pkgroot/usr/include/crug" || fail "cannot link /usr/include/crug"
ln -s /opt/crug/lib/libcrug.a "$pkgroot/usr/lib/x86_64-linux-gnu/libcrug.a" \
  || fail "cannot link /usr/lib/x86_64-linux-gnu/libcrug.a"

# The maintainer comes from the caller's Debian environment, never from the
# build host's git identity: a package must not change because of who checked
# the tree out.  Set both DEBFULLNAME and DEBEMAIL, or neither.
if [ -n "${DEBFULLNAME:-}" ] && [ -n "${DEBEMAIL:-}" ]; then
	maintainer="${DEBFULLNAME} <${DEBEMAIL}>"
elif [ -n "${DEBFULLNAME:-}${DEBEMAIL:-}" ]; then
	fail "set both DEBFULLNAME and DEBEMAIL, or neither"
else
	maintainer='cleg <chri.legendre06@gmail.com>'
fi

install -m 0644 "$ROOT/packaging/debian/copyright.in" "$pkgroot/usr/share/doc/crug/copyright" \
  || fail "cannot install the copyright file"
changelog=$pkgroot/usr/share/doc/crug/changelog.Debian
{
  printf 'crug (%s-%s) unstable; urgency=medium\n\n' "$version" "$revision"
  printf '  * Release %s.\n' "$version"
  printf '  * See /opt/crug/share/doc/crug/CHANGELOG.md for the upstream notes.\n\n'
  printf ' -- %s  %s\n' "$maintainer" "$(LC_ALL=C TZ=UTC date '+%a, %d %b %Y %H:%M:%S +0000')"
} >"$changelog" || fail "cannot write changelog.Debian"
gzip -9 -n -c "$changelog" >"$changelog.gz" || fail "cannot compress changelog.Debian"
chmod 0644 "$changelog.gz"
rm -f "$changelog"

size=$(du -sk "$pkgroot/opt" "$pkgroot/usr" | awk '{ s += $1 } END { printf "%d", s }')

awk -v v="$version-$revision" -v a="$arch" -v m="$maintainer" -v d="$depends" -v s="$size" '
  { gsub(/@VERSION@/, v); gsub(/@ARCH@/, a); gsub(/@MAINTAINER@/, m)
    gsub(/@DEPENDS@/, d); gsub(/@INSTALLED_SIZE@/, s); print }' \
  "$ROOT/packaging/debian/control.in" >"$pkgroot/DEBIAN/control" \
  || fail "cannot render DEBIAN/control"
chmod 0755 "$pkgroot/DEBIAN"
chmod 0644 "$pkgroot/DEBIAN/control"

# dpkg-deb ignores SOURCE_DATE_EPOCH, so normalise the payload mtimes here.
# Directory permissions are normalised too: the GCC install tree is built with
# the packager's umask, and a system package should not ship group-writable
# directories.  Only the owner execute bit distinguishes programs from data.
find "$pkgroot" -type d -exec chmod 0755 {} + 2>/dev/null || warn "cannot normalise the directory modes"
find "$pkgroot" -type f ! -perm -u+x -exec chmod 0644 {} + 2>/dev/null || warn "cannot normalise the file modes"
find "$pkgroot" -type f -perm -u+x -exec chmod 0755 {} + 2>/dev/null || warn "cannot normalise the program modes"
find "$pkgroot" -exec touch -h -d @0 {} + 2>/dev/null || warn "cannot normalise the payload mtimes"
( cd "$pkgroot" && find . -type f ! -path './DEBIAN/*' -printf '%P\n' | LC_ALL=C sort \
    | while IFS= read -r f; do md5sum "$f"; done ) >"$pkgroot/DEBIAN/md5sums" \
  || fail "cannot write DEBIAN/md5sums"
chmod 0644 "$pkgroot/DEBIAN/md5sums"

# ---- the package -------------------------------------------------------------
mkdir -p "$out" || fail "cannot create the output directory $out"
deb=crug_${version}-${revision}_${arch}.deb
say "building $out/$deb (dpkg-deb -Zxz -z6 --root-owner-group)"
rm -f "$out/$deb"
dpkg-deb --build --root-owner-group -Zxz -z6 "$pkgroot" "$out/$deb" >/dev/null \
  || fail "dpkg-deb failed"
chmod 0644 "$out/$deb"

tools_check=$(dpkg-deb --info "$out/$deb" 2>/dev/null | sed -n 's/^ Package: //p')
[ "$tools_check" = crug ] || fail "the package does not report Package: crug"

bytes=$(wc -c <"$out/$deb" | tr -d ' ')
files=$(dpkg-deb --contents "$out/$deb" | awk '$1 !~ /^d/ { n++ } END { print n + 0 }')
case $out in
  "$ROOT"/*) disp=${out#"$ROOT"/} ;;
  *) disp=$out ;;
esac

# ---- the checksums -----------------------------------------------------------
artefacts=
for a in "crug-$version-linux-x86_64.tar.gz" "crug-$version.spdx.json" "$deb" LICENSE NOTICE; do
  [ -f "$out/$a" ] && artefacts="$artefacts $a"
done
( cd "$out" && sha256sum $artefacts >SHA256SUMS ) || fail "cannot write SHA256SUMS"
chmod 0644 "$out/SHA256SUMS"
n=$(wc -l <"$out/SHA256SUMS" | tr -d ' ')

say "$disp/$deb $bytes bytes, $files files, installed-size $size KiB"
say "depends: $depends"
say "SHA256SUMS: $n artefact(s)"
exit 0
