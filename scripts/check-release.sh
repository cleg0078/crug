#!/bin/sh
# check-release.sh - verify the Phase 24 release artefacts.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# Reads VERSION, CHANGELOG.md, gcc/crugc --version, dist/SHA256SUMS,
# dist/crug-<version>.spdx.json, the release tarball and the Debian package,
# and checks that they all name the same version, that every checksum
# verifies, that the SBOM is a parseable SPDX 2.3 document, and that the
# tarball carries the files the Phase 24 gate looks for. It changes nothing;
# scripts/check-deb.sh inspects the package itself.
#
# Usage: scripts/check-release.sh
#
# Exit status: 0 every check held, 1 at least one problem (each is printed).

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1
cd "$ROOT" || exit 1

out=$ROOT/dist
tarball_name=
sbom_name=
checks=0
problems=0

problem () {
  problems=$((problems + 1))
  printf 'check-release: problem: %s\n' "$*"
}

checked () {
  checks=$((checks + 1))
}

finish () {
  printf 'check-release: %d check(s), %d problem(s)\n' "$checks" "$problems"
  [ "$problems" -eq 0 ] || exit 1
  exit 0
}

for tool in tar gzip sha256sum python3 sed awk grep mktemp wc; do
  command -v "$tool" >/dev/null 2>&1 || {
    problem "required tool '$tool' is not on PATH"
    finish
  }
done

tmp=$(mktemp -d) || {
  problem 'cannot create a temporary directory'
  finish
}
trap 'rm -rf "$tmp"' 0 1 2 15

# ---- 1. the version the three sources agree on -------------------------------
checked
version=
if [ ! -s "$ROOT/VERSION" ]; then
  problem 'VERSION is missing or empty'
else
  version=$(sed -n '1p' "$ROOT/VERSION")
  case $version in
    ""|*[!0-9.]*|.*|*.)
      problem "VERSION is not a dotted release version: '$version'"
      version=
      ;;
  esac
fi
[ -n "$version" ] || finish

tarball_name=crug-$version-linux-x86_64.tar.gz
sbom_name=crug-$version.spdx.json

checked
changelog_version=
if [ ! -s "$ROOT/CHANGELOG.md" ]; then
  problem 'CHANGELOG.md is missing or empty'
else
  changelog_version=$(sed -n \
    's/^##[[:space:]]*\[\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)\([^]]*\)\].*/\1\2/p' \
    "$ROOT/CHANGELOG.md" | sed -n '1p')
  # A release candidate entry names $version-rcN while VERSION keeps the
  # release number, so accept both forms.
  case $changelog_version in
    "$version"|"$version"-rc[0-9]*) ;;
    "")
      problem 'CHANGELOG.md opens with no "## [x.y.z]" release entry'
      ;;
    *)
      problem "CHANGELOG.md names $changelog_version but VERSION names $version"
      ;;
  esac
fi

checked
crugc_out=
if [ -f "$ROOT/gcc/crugc" ]; then
  crugc_out=$(sh "$ROOT/gcc/crugc" --version 2>/dev/null) || crugc_out=
fi
crugc_first=$(printf '%s\n' "$crugc_out" | sed -n '1p')
if [ "$crugc_first" != "crugc $version" ]; then
  problem "gcc/crugc --version first line is '$crugc_first', not 'crugc $version'"
fi

checked
crugc_lines=$(printf '%s\n' "$crugc_out" | awk 'NF { n++ } END { print n + 0 }')
crugc_labels='compiler: GCC |language version: |Crug ABI version: |runtime ABI version: |FFI metadata version: |PAL version: '
if [ "$crugc_lines" -ne 7 ]; then
  problem "gcc/crugc --version prints $crugc_lines line(s), not the seven version lines"
else
  old_ifs=$IFS
  IFS='|'
  for label in $crugc_labels; do
    printf '%s\n' "$crugc_out" | grep -Fq "$label" \
      || problem "gcc/crugc --version does not print '$label'"
  done
  IFS=$old_ifs
fi

# ---- 2. the dist directory and its checksums ---------------------------------
checked
if [ ! -d "$out" ]; then
  problem "no artefact directory at $out (run scripts/package.sh first)"
  finish
fi

checked
if [ ! -s "$out/SHA256SUMS" ]; then
  problem "no $out/SHA256SUMS (run scripts/package.sh first)"
  finish
fi

checked
deb_name=
for candidate in "$out"/crug_"$version"-*_amd64.deb; do
  [ -f "$candidate" ] || continue
  [ -z "$deb_name" ] || problem "more than one Debian package in $out"
  [ -n "$deb_name" ] || deb_name=$(basename "$candidate")
done
[ -n "$deb_name" ] || problem "no $out/crug_$version-<revision>_amd64.deb (run scripts/package.sh)"

checked
expected_names=$(printf '%s\n' "$tarball_name" "$sbom_name" LICENSE NOTICE "$deb_name" \
  | sed '/^$/d' | LC_ALL=C sort)
listed_names=$(awk '{ print $2 }' "$out/SHA256SUMS" | sed 's/^\*//' | LC_ALL=C sort)
if [ "$expected_names" != "$listed_names" ]; then
  problem "SHA256SUMS does not list exactly $tarball_name, $sbom_name, the Debian package, LICENSE and NOTICE"
fi

checked
if ! ( cd "$out" && sha256sum -c SHA256SUMS ) >"$tmp/checksums.log" 2>&1; then
  problem 'sha256sum -c SHA256SUMS does not verify every artefact'
  sed -n 's/^/check-release:   /p' "$tmp/checksums.log" | head -n 10
fi

checked
[ -f "$out/$tarball_name" ] || problem "missing $out/$tarball_name"
checked
[ -f "$out/$sbom_name" ] || problem "missing $out/$sbom_name"
checked
[ -s "$out/LICENSE" ] || problem "missing or empty $out/LICENSE"
checked
[ -s "$out/NOTICE" ] || problem "missing or empty $out/NOTICE"
checked
if [ -z "$deb_name" ] || [ ! -s "$out/$deb_name" ]; then
  problem "missing or empty $out/${deb_name:-crug_$version-<revision>_amd64.deb}"
fi
checked
if [ -n "$deb_name" ]; then
  deb_bytes=$(wc -c <"$out/$deb_name" | tr -d ' ')
  [ "$deb_bytes" -gt 10000000 ] \
    || problem "the Debian package is only $deb_bytes bytes, which is too small for a compiler"
fi

# ---- 3. the SBOM ---------------------------------------------------------------
checked
if [ ! -s "$out/$sbom_name" ]; then
  problem "no $out/$sbom_name to inspect"
elif ! python3 - "$out/$sbom_name" >"$tmp/sbom.fields" 2>"$tmp/sbom.err" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    document = json.load(handle)
packages = {package.get("name"): package for package in document.get("packages") or []}
fields = {
    "spdxVersion": document.get("spdxVersion", ""),
    "dataLicense": document.get("dataLicense", ""),
    "name": document.get("name", ""),
    "namespace": document.get("documentNamespace", ""),
    "crug_version": (packages.get("crug") or {}).get("versionInfo", ""),
    "has_gcc": "1" if any(n.startswith("gcc-") for n in packages) else "0",
    "has_libcrug": "1" if "libcrug" in packages else "0",
}
for key, value in fields.items():
    print("%s=%s" % (key, value))
PY
then
    problem "the SBOM does not parse as JSON: $(sed -n '1p' "$tmp/sbom.err")"
    finish
fi
sbom_field () {
  sed -n "s/^$1=//p" "$tmp/sbom.fields" | sed -n '1p'
}

checked
[ "$(sbom_field spdxVersion)" = "SPDX-2.3" ] \
  || problem "the SBOM spdxVersion is '$(sbom_field spdxVersion)', not SPDX-2.3"
checked
[ "$(sbom_field dataLicense)" = "CC0-1.0" ] \
  || problem "the SBOM dataLicense is '$(sbom_field dataLicense)', not CC0-1.0"
checked
[ "$(sbom_field name)" = "crug-$version" ] \
  || problem "the SBOM name is '$(sbom_field name)', not crug-$version"
checked
if [ -f "$out/$tarball_name" ]; then
  tarball_sha=$(sha256sum "$out/$tarball_name" | awk '{ print $1 }')
  case $(sbom_field namespace) in
    *"$tarball_sha"*) ;;
    *) problem 'the SBOM document namespace does not embed the tarball SHA256' ;;
  esac
fi
checked
[ "$(sbom_field crug_version)" = "$version" ] \
  || problem "the SBOM crug package names '$(sbom_field crug_version)', not $version"
checked
[ "$(sbom_field has_gcc)" = 1 ] || problem 'the SBOM has no gcc-<version> package'
checked
[ "$(sbom_field has_libcrug)" = 1 ] || problem 'the SBOM has no libcrug package'

# ---- 4. the tarball ------------------------------------------------------------
checked
if [ ! -f "$out/$tarball_name" ]; then
  problem "no $out/$tarball_name to list"
else
  if ! tar -tzf "$out/$tarball_name" >"$tmp/tar.list" 2>"$tmp/tar.err"; then
    problem "the tarball does not list: $(sed -n '1p' "$tmp/tar.err")"
  else
    missing=
    for path in \
      "crug-$version/bin/crugc" \
      "crug-$version/include/crug/task.h" \
      "crug-$version/include/crug/chan.h" \
      "crug-$version/include/crug/crug_pal.h" \
      "crug-$version/lib/libcrug.a" \
      "crug-$version/share/doc/crug/LICENSE" \
      "crug-$version/share/doc/crug/NOTICE" \
      "crug-$version/share/doc/crug/CHANGELOG.md" \
      "crug-$version/share/doc/crug/VERSION"
    do
      grep -Fxq "$path" "$tmp/tar.list" || missing="$missing $path"
    done
    [ -z "$missing" ] || problem "the tarball is missing:$missing"
  fi
fi

finish
