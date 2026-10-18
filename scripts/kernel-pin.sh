#!/usr/bin/env bash
# Kernel pin helper for docs/crug/KERNEL-BASELINE.md.
#
# The pin is one artefact, linux-6.18.57.tar.xz, with the SHA256 recorded in
# docs/crug/kernel-baseline.sha256. This script downloads it, checks it and
# prints the pinned values; it never builds or boots anything.
#
# Usage:
#   scripts/kernel-pin.sh --show     print the pinned tag, commit, URL and digest
#   scripts/kernel-pin.sh --fetch    download the tarball into sources/kernel/
#   scripts/kernel-pin.sh --verify   verify sources/kernel against the recorded digest
#   scripts/kernel-pin.sh            default: --verify
#
# Options:
#   --help       print this usage and exit
#
# Exit status: 0 on success, 1 when the download or the digest check fails, 2 on
# usage or repository errors.
set -euo pipefail

version=6.18.57
tag=v6.18.57
commit=2ff600d29ee478d0a8d46ef8a58afb5e038fe692
tarball=linux-6.18.57.tar.xz
url=https://cdn.kernel.org/pub/linux/kernel/v6.x/$tarball

DOC_REL="docs/crug/KERNEL-BASELINE.md"
SUMS_REL="docs/crug/kernel-baseline.sha256"
SRC_REL="sources/kernel"

ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || { echo "kernel-pin.sh: not inside a git work tree" >&2; exit 2; }
DOC="$ROOT/$DOC_REL"
SUMS="$ROOT/$SUMS_REL"
SRC="$ROOT/$SRC_REL"
[[ -f $DOC ]] || { echo "kernel-pin.sh: missing $DOC_REL" >&2; exit 2; }
[[ -f $SUMS ]] || { echo "kernel-pin.sh: missing $SUMS_REL" >&2; exit 2; }

mode=verify
while (($#)); do
  case $1 in
    --show) mode=show ;;
    --fetch) mode=fetch ;;
    --verify) mode=verify ;;
    -h|--help) sed -n "2,18p" "$0"; exit 0 ;;
    -*) echo "kernel-pin.sh: unknown option $1" >&2; exit 2 ;;
    *) echo "kernel-pin.sh: unexpected argument $1" >&2; exit 2 ;;
  esac
  shift
done

digest=$(awk -v f="$tarball" '$2 == f { print $1; n++ } END { exit n == 1 ? 0 : 1 }' "$SUMS") \
  || { echo "kernel-pin.sh: $SUMS_REL must name $tarball exactly once" >&2; exit 2; }
[[ $digest =~ ^[0-9a-f]{64}$ ]] || { echo "kernel-pin.sh: malformed digest in $SUMS_REL" >&2; exit 2; }

show() {
  local v
  for v in "$version" "$tag" "$commit" "$digest" "$url"; do
    grep -qF -- "$v" "$DOC" \
      || { echo "kernel-pin.sh: $DOC_REL does not mention $v" >&2; exit 2; }
  done
  printf '%-9s %s\n' \
    kernel "$version" \
    tag "$tag" \
    commit "$commit" \
    tarball "$url" \
    sha256 "$digest" \
    checksum "$SUMS_REL"
}

fetch() {
  mkdir -p "$SRC"
  if [[ -f $SRC/$tarball ]] && (cd "$SRC" && sha256sum --check --strict --quiet "$SUMS") 2>/dev/null; then
    echo "kernel-pin.sh: sources/kernel/$tarball already matches the pin"
    return 0
  fi
  curl --fail --location --retry 3 --continue-at - --output "$SRC/$tarball" "$url"
  echo "kernel-pin.sh: fetched sources/kernel/$tarball"
}

verify() {
  local out
  [[ -f $SRC/$tarball ]] \
    || { echo "kernel-pin.sh: missing sources/kernel/$tarball (run --fetch)" >&2; exit 1; }
  out=$(cd "$SRC" && sha256sum --check --strict "$SUMS" 2>&1) || {
    printf '%s\n' "$out" >&2
    echo "kernel-pin.sh: digest check failed; the tree is not the pin" >&2
    exit 1
  }
  printf '%s\n' "$out"
  echo "kernel-$version: OK"
}

case $mode in
  show) show ;;
  verify) verify ;;
  fetch) fetch && verify ;;
esac
