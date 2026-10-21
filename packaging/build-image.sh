#!/bin/sh
# build-image.sh - build the Phase 24 container image crug/pkg:<version>.
#
# The sandbox cannot use buildx, so force the legacy builder.  The legacy
# builder uploads the whole build context; this checkout keeps ~19 GiB of
# build/ artefacts and .git next to the sources, so stage a pruned copy of
# the tree under build/packaging/context and build from there.  The Dockerfile
# itself is the ordinary multi-stage one in packaging/Dockerfile.
#
#   ./packaging/build-image.sh
#   docker run --rm crug/pkg:0.1.0 crugc --version

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=$(cat "$root/VERSION")
tag=${CRUG_IMAGE_TAG:-crug/pkg:$version}
context="$root/build/packaging/context"

if ! command -v docker >/dev/null 2>&1; then
  echo "build-image.sh: docker not found" >&2
  exit 1
fi
if [ ! -f "$root/scripts/package.sh" ]; then
  echo "build-image.sh: scripts/package.sh is missing (the image installs" >&2
  echo "build-image.sh: the compiler with it); nothing to build yet" >&2
  exit 1
fi

echo "build-image.sh: staging the build context in $context"
rm -rf "$context"
mkdir -p "$context"
rsync -a --delete \
      --exclude '/.git' \
      --exclude '/build' \
      --exclude '/sources' \
      --exclude '/packaging/Dockerfile' \
      --exclude '/packaging/build-image.sh' \
      "$root"/ "$context"/

echo "build-image.sh: building $tag (legacy builder, host network)"
DOCKER_BUILDKIT=0 docker build \
  --network host \
  -f "$root/packaging/Dockerfile" \
  -t "$tag" \
  "$context"

printf '%s\n' "$tag"
