#!/bin/sh
# check-packaging.sh - the Phase 24 packaging acceptance check.
#
# Runs the whole release flow and then verifies it from the outside:
#   1. scripts/package.sh     - stage, tarball, SBOM, checksums into dist/
#   2. scripts/check-release.sh - version consistency, checksums, tarball shape
#   3. the SBOM and the tarball listing, checked here independently
#   4. the container image: build it when absent, then run "crugc --version"
#      and the three smoke programs (plain C, ownership, runtime) inside it.
#   5. the Debian package: install it with apt in a clean ubuntu:24.04, run
#      the three smoke programs through /usr/bin/crugc, then remove it again.
#
# Exit status: 0 everything passed, 1 a check failed, 2 a missing tool/artefact
# that is not a verdict (docker absent, python3 absent, image directory absent).

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=$(cat "$root/VERSION" 2>/dev/null) || {
  printf '%s\n' "packaging: ERROR: missing $root/VERSION"
  exit 2
}
tag="crug/pkg:$version"
dist="$root/dist"
tarball="$dist/crug-$version-linux-x86_64.tar.gz"
sbom="$dist/crug-$version.spdx.json"
build="$root/build/packaging"
failed=0
artefacts=0

say () { printf '%s\n' "$*"; }
bad () { say "packaging: FAIL: $*"; failed=1; }
missing () { say "packaging: ERROR: $*"; exit 2; }

command -v python3 >/dev/null 2>&1 || missing "python3 is not available"
mkdir -p "$build" || missing "cannot create $build"

# ---- 1. build the release artefacts. ----
say "packaging: version $version"
if ! sh "$root/scripts/package.sh"; then
  bad "scripts/package.sh failed"
else
  artefacts=$((artefacts + 1))
fi

# ---- 2. the release checker owns version, checksum and tarball shape. ----
if [ -x "$root/scripts/check-release.sh" ]; then
  if sh "$root/scripts/check-release.sh"; then
    artefacts=$((artefacts + 1))
  else
    bad "scripts/check-release.sh failed"
  fi
else
  missing "missing $root/scripts/check-release.sh"
fi

# ---- 3. the SBOM and the tarball listing, checked here. ----
if [ -s "$sbom" ]; then
  if python3 - "$sbom" "$version" <<'PY' >>"$build/sbom-check.log" 2>&1
import json, sys
path, version = sys.argv[1], sys.argv[2]
doc = json.load(open(path, encoding="utf-8"))
problems = []
if doc.get("spdxVersion") != "SPDX-2.3":
    problems.append("spdxVersion is not SPDX-2.3")
if doc.get("dataLicense") != "CC0-1.0":
    problems.append("dataLicense is not CC0-1.0")
if doc.get("SPDXID") != "SPDXRef-DOCUMENT":
    problems.append("SPDXID is not SPDXRef-DOCUMENT")
if doc.get("name") != "crug-" + version:
    problems.append("name is not crug-" + version)
if not doc.get("documentNamespace"):
    problems.append("documentNamespace is empty")
packages = doc.get("packages") or []
names = sorted(p.get("name", "") for p in packages)
for wanted in ("crug", "gcc-16.2.0", "libcrug"):
    if wanted not in names:
        problems.append("missing package " + wanted)
for p in packages:
    if not p.get("SPDXID") or not p.get("licenseConcluded"):
        problems.append("package " + p.get("name", "?") + " lacks SPDXID or licenseConcluded")
if problems:
    print("\n".join(problems))
    sys.exit(1)
print("sbom: %d package(s): %s" % (len(packages), ", ".join(names)))
PY
  then
    artefacts=$((artefacts + 1))
  else
    bad "the SBOM is malformed (see $build/sbom-check.log)"
  fi
else
  bad "missing $sbom"
fi

if [ -s "$tarball" ]; then
  listing="$build/tarball-list.txt"
  if tar -tzf "$tarball" >"$listing" 2>"$build/tar.err"; then
    for entry in "crug-$version/bin/crugc" "crug-$version/include/crug/task.h" \
                 "crug-$version/lib/libcrug.a" "crug-$version/share/doc/crug/LICENSE" \
                 "crug-$version/share/doc/crug/NOTICE"; do
      grep -qx "$entry" "$listing" || bad "tarball lacks $entry"
    done
    artefacts=$((artefacts + 1))
  else
    bad "cannot list $tarball"
  fi
else
  bad "missing $tarball"
fi

# ---- 4. the container image. ----
if command -v docker >/dev/null 2>&1; then
  if ! docker image inspect "$tag" >/dev/null 2>&1; then
    say "packaging: image $tag not present, building it"
    if ! sh "$root/packaging/build-image.sh"; then
      bad "packaging/build-image.sh failed"
    fi
  fi
  if docker image inspect "$tag" >/dev/null 2>&1; then
    if docker run --rm "$tag" crugc --version >"$build/image-version.log" 2>&1; then
      for line in "crugc $version" "compiler: GCC 16.2.0" "language version: $version" \
                  "Crug ABI version: 1" "runtime ABI version: 1"; do
        grep -qx "$line" "$build/image-version.log" \
          || bad "image --version lacks line: $line"
      done
      artefacts=$((artefacts + 1))
    else
      bad "crugc --version failed in $tag"
    fi
    smoke_ok=0
    for t in hello.c hello-own.c runtime-sample.c; do
      case "$t" in
        hello-own.c) flags="-fcrug-ownership" ;;
        runtime-sample.c) flags="-fcrug-goroutines -fcrug-channels -I/opt/crug/include/crug -L/opt/crug/lib -lcrug -pthread" ;;
        *) flags="" ;;
      esac
      # The static archive is scanned before the object that needs it, so the
      # -L/-l pair must come after the translation unit, as in the docs.
      if docker run --rm -v "$root/packaging/smoke:/w" -w /w "$tag" \
           sh -c "crugc $t $flags -o /tmp/smoke && /tmp/smoke" \
           >"$build/smoke-$t.log" 2>&1; then
        smoke_ok=$((smoke_ok + 1))
      else
        bad "smoke program $t failed (see $build/smoke-$t.log)"
      fi
    done
    say "packaging: image smoke: $smoke_ok/3 ok"
    [ "$smoke_ok" -eq 3 ] || failed=1
  else
    bad "image $tag is missing after the build"
  fi
else
  say "packaging: docker absent, image checks skipped (not a pass, not a failure)"
fi

# ---- 5. install the Debian package in a clean container. ----
if command -v docker >/dev/null 2>&1; then
  deb=
  for candidate in "$dist"/crug_"$version"-*_amd64.deb; do
    [ -f "$candidate" ] || continue
    deb=$(basename "$candidate")
  done
  if [ -z "$deb" ]; then
    bad "no $dist/crug_$version-<revision>_amd64.deb to install"
  else
    say "packaging: installing $deb in a clean ubuntu:24.04"
    docker run --rm --network host \
      -v "$dist:/pkg:ro" -v "$root/packaging/smoke:/w:ro" -w /tmp \
      ubuntu:24.04 sh -c '
set -e
deb=$1
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq "/pkg/$deb" >/dev/null 2>&1
crugc --version > version.txt
test "$(grep -c . version.txt)" -eq 7
grep -qx "crugc '"$version"'" version.txt
grep -qx "compiler: GCC 16.2.0" version.txt
grep -qx "Crug ABI version: 1" version.txt
dpkg -L crug | grep -qx /opt/crug/bin/crugc
test -L /usr/bin/crugc
test "$(readlink -f /usr/bin/crugc)" = /opt/crug/bin/crugc
crugc /w/hello.c -o hello
[ "$(./hello)" = "hello from crug" ]
echo "deb-smoke: hello ok"
crugc /w/hello-own.c -fcrug-ownership -o hello-own
[ "$(./hello-own)" = "own-ok value=42" ]
echo "deb-smoke: hello-own ok"
crugc /w/runtime-sample.c -fcrug-goroutines -fcrug-channels \
  -I/opt/crug/include/crug -L/opt/crug/lib -lcrug -pthread -o runtime
[ "$(./runtime)" = "runtime-ok sum=7" ]
echo "deb-smoke: runtime ok"
apt-get remove -y -qq crug >/dev/null 2>&1 || true
test ! -e /usr/bin/crugc
test ! -e /opt/crug/bin/crugc
' sh "$deb" >"$build/deb-install.log" 2>&1
    deb_ok=$(grep -c "^deb-smoke: .* ok\$" "$build/deb-install.log" 2>/dev/null || echo 0)
    say "packaging: deb install smoke: $deb_ok/3 ok"
    if [ "$deb_ok" -ne 3 ]; then
      bad "the Debian install smoke failed (see $build/deb-install.log)"
    fi
  fi
else
  say "packaging: docker absent, Debian install smoke skipped"
fi

say "packaging: $artefacts artefact set(s) verified, tarball $(wc -c <"$tarball" 2>/dev/null || echo 0) bytes"
if [ "$failed" -eq 0 ] && [ "$artefacts" -ge 3 ]; then
  say "packaging: OK"
  exit 0
fi
say "packaging: FAIL"
exit 1
