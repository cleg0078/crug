#!/bin/sh
# check-kernel-objects.sh - Phase 2: no kernel object may reference the Crug
# runtime, either through an __crug_ symbol or a libcrug dependency.
#
# Usage: scripts/check-kernel-objects.sh [object-dir]   (default: out)
# Exit: 0 clean; 1 a runtime reference was found; 2 no objects or no binutils.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=${1:-out}
case $DIR in
/*) ;;
*) DIR=$ROOT/$DIR ;;
esac

command -v nm >/dev/null 2>&1 || { echo "kernel-objects: nm not found" >&2; exit 2; }
command -v readelf >/dev/null 2>&1 || { echo "kernel-objects: readelf not found" >&2; exit 2; }
[ -d "$DIR" ] || { echo "kernel-objects: $DIR does not exist" >&2; exit 2; }

count=0
bad=0
for o in "$DIR"/*.o; do
	[ -f "$o" ] || continue
	count=$((count + 1))
	if nm "$o" 2>/dev/null | awk '{print $NF}' | grep -q '^__crug_'; then
		echo "kernel-objects: $o references an __crug_ symbol" >&2
		bad=1
	fi
	if readelf -d "$o" 2>/dev/null | grep -q 'libcrug'; then
		echo "kernel-objects: $o depends on libcrug" >&2
		bad=1
	fi
done

if [ "$count" -eq 0 ]; then
	echo "kernel-objects: no objects under $DIR (run scripts/test-kernel-compat.sh first)" >&2
	exit 2
fi

if [ "$bad" -eq 0 ]; then
	echo "kernel-objects: $count objects, __crug_=0, libcrug=0"
	exit 0
fi
exit 1
