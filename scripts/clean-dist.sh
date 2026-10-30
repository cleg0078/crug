#!/bin/sh
# clean-dist.sh - prune release artefacts of versions other than VERSION.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# The release directory accumulates one tarball, one SBOM and one Debian
# package per version.  Only the current version's artefacts belong there:
# scripts/check-release.sh requires SHA256SUMS to list exactly the current
# tarball, SBOM, Debian package, LICENSE and NOTICE, and a stale artefact
# makes "which file is the release?" ambiguous for everyone else.  This
# script keeps the current five, rewrites SHA256SUMS over them, deletes the
# release files and archive directories of every other version, and leaves
# files it does not recognise alone.
#
# It refuses to run when an artefact of the current version is missing,
# because rewriting SHA256SUMS then would describe an incomplete release.
# Run it with --dry-run first to review the removals.
#
# Usage: scripts/clean-dist.sh [--dist DIR] [--dry-run]
#
#   --dist DIR   artefact directory (default: dist/)
#   --dry-run    print what would be removed and kept, change nothing
#
# Exit status: 0 pruned (or nothing to prune), 1 the rewritten checksums do
# not verify, 2 a prerequisite is missing or the directory is ambiguous.
#
# The release flow itself is scripts/package.sh; verify a pruned directory
# with scripts/check-release.sh.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

dist=dist
dry_run=0

say () { printf 'clean-dist: %s\n' "$*"; }
warn () { printf 'clean-dist: %s\n' "$*" >&2; }
die () { printf 'clean-dist: %s\n' "$*" >&2; exit 2; }
fail () { printf 'clean-dist: %s\n' "$*" >&2; exit 1; }

usage () {
  sed -n '2,/^set -u$/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
  case $1 in
    --dist)
      [ $# -ge 2 ] || die 'option --dist needs a directory'
      dist=$2
      shift 2
      ;;
    --dry-run)
      dry_run=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1 (try --help)"
      ;;
  esac
done

version=$(cat VERSION 2>/dev/null) || die 'missing VERSION'
[ -n "$version" ] || die 'VERSION is empty'
[ -d "$dist" ] || die "no artefact directory at $dist (run scripts/package.sh first)"
dist=$(CDPATH= cd -- "$dist" && pwd) || die "cannot enter $dist"

tarball=crug-$version-linux-x86_64.tar.gz
sbom=crug-$version.spdx.json
deb=
for candidate in "$dist"/crug_"$version"-*_amd64.deb; do
  [ -f "$candidate" ] || continue
  [ -z "$deb" ] || die "more than one Debian package for $version in $dist"
  deb=$(basename -- "$candidate")
done
[ -n "$deb" ] || die "no $dist/crug_$version-<revision>_amd64.deb (run make package)"
for name in "$tarball" "$sbom" "$deb" LICENSE NOTICE; do
  [ -s "$dist/$name" ] || die "missing or empty $dist/$name (run make package)"
done

say "version $version, artefact directory $dist"
removed=0
for path in "$dist"/*; do
  [ -e "$path" ] || continue
  base=$(basename -- "$path")
  case $base in
    "$tarball"|"$sbom"|"$deb"|LICENSE|NOTICE|SHA256SUMS)
      continue
      ;;
  esac
  case $path in
    "$dist"/*) ;;
    *) die "refusing to touch $path, which is outside $dist" ;;
  esac
  stale=0
  if [ -d "$path" ]; then
    case $base in
      archive-*) stale=1 ;;
    esac
  else
    case $base in
      crug-*.tar.gz|crug-*.spdx.json|crug_*.deb) stale=1 ;;
    esac
  fi
  if [ "$stale" -eq 0 ]; then
    warn "leaving unrecognised entry $path alone"
    continue
  fi
  if [ "$dry_run" -eq 1 ]; then
    say "would remove $base"
  else
    rm -rf -- "$path" || fail "cannot remove $path"
    say "removed $base"
  fi
  removed=$((removed + 1))
done

if [ "$dry_run" -eq 1 ]; then
  say "would rewrite SHA256SUMS over $tarball, $sbom, $deb, LICENSE and NOTICE"
  say "dry run: $removed entr$([ "$removed" -eq 1 ] && echo y || echo ies) would be removed"
  exit 0
fi

tmp=$dist/.SHA256SUMS.$$
if ! ( cd "$dist" && LC_ALL=C sha256sum -- "$tarball" "$sbom" "$deb" LICENSE NOTICE         | LC_ALL=C sort -k2 ) >"$tmp"; then
  rm -f -- "$tmp"
  fail 'cannot compute the checksums'
fi
mv -- "$tmp" "$dist/SHA256SUMS" || { rm -f -- "$tmp"; fail 'cannot install SHA256SUMS'; }

if ! ( cd "$dist" && sha256sum -c SHA256SUMS ) >"$dist/.check.$$" 2>&1; then
  cat "$dist/.check.$$" >&2
  rm -f -- "$dist/.check.$$"
  fail 'the rewritten SHA256SUMS does not verify'
fi
rm -f -- "$dist/.check.$$"
say "kept $tarball, $sbom, $deb, LICENSE and NOTICE; rewrote SHA256SUMS; $removed entr$([ "$removed" -eq 1 ] && echo y || echo ies) removed"
exit 0
