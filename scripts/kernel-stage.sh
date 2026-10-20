#!/bin/sh
# kernel-stage.sh - Phase 19 kettle stages K1-K7 for the pinned Linux tree.
#
# The Linux kernel is an external corpus, not a vendored copy: it lives in an
# ignored directory and is pinned by docs/crug/KERNEL-BASELINE.md. This script
# builds and boots that tree with the Crug driver and records one observable per
# roadmap section 24 stage:
#
#   K1  selected headers   every probe TU under kernel/probes/ compiles as an
#                          external module against the pinned tree
#   K2  selected TUs       twelve translation units from the gate's subsystems
#   K3  one subsystem      lib/ is built as a whole
#   K4  object corpus      the pinned object list of kernel/KERNEL-CORPUS.md
#   K5  full build         the whole tree with CC=crugc; vmlinux, bzImage and
#                          System.map must exist
#   K6  boot smoke         the built kernel boots in qemu and runs a
#                          freestanding /init that prints crug-boot-ok and
#                          powers off
#   K7  second arch        a pinned aarch64 subset compiles with a cross
#                          compiler built from this same tree
#
# The stages share one out-of-tree object directory, so running them in order
# reuses objects; each stage still forces the files it claims to have compiled.
#
# Usage:
#   scripts/kernel-stage.sh K1|K2|K3|K4|K5|K6|K7
#   scripts/kernel-stage.sh all        run K1 K2 K3 K4 K5 K6 K7 in order
#
# Environment:
#   KSRC=dir    extracted pinned kernel tree   (default build/linux-6.18.57)
#   KO=dir      x86_64 object directory        (default build/kernel-crug)
#   KO_ARM=dir  aarch64 object directory       (default build/kernel-arm64)
#   CRUGC=drv   compiler driver                (default gcc/crugc)
#   CROSS=drv   aarch64 cross driver           (default build/cross-aarch64/gcc/xgcc)
#   JOBS=n      parallelism                    (default: online CPUs)
#
# Exit status: 0 the stage passed, 1 a stage check failed, 2 a prerequisite or
# an invocation is missing. The kernel tree itself is never modified.
#
# The Phase 19 gate runs the stages through the pre-commit driver:
#   scripts/kernel-stage.sh K1 ... (see docs/crug/EXIT-GATES.md)
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NAME=kernel-stage
CORPUS=$ROOT/kernel/KERNEL-CORPUS.md
KERNEL_VERSION=6.18.57

KSRC=${KSRC:-$ROOT/build/linux-6.18.57}
KO=${KO:-$ROOT/build/kernel-crug}
KO_ARM=${KO_ARM:-$ROOT/build/kernel-arm64}
CRUGC=${CRUGC:-$ROOT/gcc/crugc}
CROSS=${CROSS:-$ROOT/build/cross-aarch64/gcc/xgcc}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
STAGES=${STAGES:-$ROOT/build/kernel-stages}

# Evidence goes to stdout, failure explanations to stderr; the gate archives
# both. "say" lines are the observable of a passing stage.
say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }
fail () { warn "$*"; exit 1; }

usage () {
	sed -n '2,45p' "$0"
}

stage=${1:-}
case $stage in
K1|K2|K3|K4|K5|K6|K7|all) ;;
-h|--help|'') usage; exit 2 ;;
*) warn "unknown stage '$stage' (want K1..K7 or all)"; exit 2 ;;
esac

[ -x "$CRUGC" ] || die "no compiler driver at $CRUGC"
[ -d "$KSRC" ] || die "no kernel tree at $KSRC (extract the pin: scripts/kernel-pin.sh --fetch)"
[ -f "$KSRC/Makefile" ] || die "$KSRC is not a kernel tree"
mkdir -p "$STAGES" || die "cannot create $STAGES"

# The kernel's host tools (objtool) need libelf headers. This checkout keeps a
# root-free copy under build/deps; use it when it is there, otherwise rely on
# the host. Failures downstream name the missing header, so no probing here.
if [ -d "$ROOT/build/deps/usr/include" ]; then
	CPATH=${CPATH:+$CPATH:}$ROOT/build/deps/usr/include
	export CPATH
	if [ -d "$ROOT/build/deps/lib" ]; then
		LIBRARY_PATH=${LIBRARY_PATH:+$LIBRARY_PATH:}$ROOT/build/deps/lib
		export LIBRARY_PATH
	fi
fi

# Section 5 fixes one configuration and one architecture for K1-K6; section 6
# requires the digest to be verified before the tree produces any result.
check_tree () {
	kv=$(awk -F= '/^VERSION[ \t]*=/    { v=$2; gsub(/[ \t]/, "", v) }
	               /^PATCHLEVEL[ \t]*=/ { p=$2; gsub(/[ \t]/, "", p) }
	               /^SUBLEVEL[ \t]*=/   { s=$2; gsub(/[ \t]/, "", s) }
	               END { print v "." p "." s }' "$KSRC/Makefile")
	[ "$kv" = "$KERNEL_VERSION" ] \
		|| die "$KSRC is Linux $kv, the pin is $KERNEL_VERSION (KERNEL-BASELINE.md section 5)"
}

verify_pin () {
	if [ -f "$ROOT/sources/kernel/linux-6.18.57.tar.xz" ]; then
		"$ROOT/scripts/kernel-pin.sh" --verify >/dev/null 2>&1 \
			|| fail "the pinned tarball failed its digest check (KERNEL-BASELINE.md section 6.2)"
		say "pin ok: linux-$KERNEL_VERSION digest verified"
	else
		warn "no pinned tarball under sources/kernel; cannot re-verify the digest of $KSRC"
	fi
}

x86_make () {
	make -C "$KSRC" O="$KO" -j"$JOBS" CC="$CRUGC" "$@"
}

ensure_x86_config () {
	[ -f "$KO/.config" ] && return 0
	mkdir -p "$KO" || die "cannot create $KO"
	say "configuring: x86_64_defconfig in $KO"
	x86_make x86_64_defconfig >"$STAGES/defconfig-x86.log" 2>&1 \
		|| { tail -n 20 "$STAGES/defconfig-x86.log" >&2
		     fail "x86_64_defconfig failed"; }
}

# ---------------------------------------------------------------- K1 headers
probe_dir=$STAGES/k1/probes

stage_k1 () {
	ensure_x86_config
	rm -rf "$STAGES/k1"
	mkdir -p "$probe_dir" || die "cannot create $probe_dir"
	# Build a copy: Kbuild writes .o/.ko/.mod* next to the sources, and the
	# committed probe directory stays clean.
	cp "$ROOT"/kernel/probes/Makefile "$ROOT"/kernel/probes/probe-*.c "$probe_dir"/ \
		|| die "cannot copy kernel/probes"
	say "K1: compiling kernel/probes as an external module"
	if ! x86_make M="$probe_dir" KBUILD_MODPOST_WARN=1 modules \
			>"$STAGES/k1/build.log" 2>&1; then
		tail -n 40 "$STAGES/k1/build.log" >&2
		fail "K1: the header probes did not compile"
	fi
	n=0
	for src in "$probe_dir"/probe-*.c; do
		case $src in *.mod.c) continue ;; esac
		[ -s "${src%.c}.o" ] || fail "K1: no object for ${src##*/}"
		n=$((n + 1))
	done
	say "K1: $n header probe(s) compiled"
}

# ------------------------------------------------------- K2 selected objects
k2_objects="lib/string.o kernel/fork.o kernel/sched/core.o mm/page_alloc.o
            mm/slub.o fs/read_write.o fs/inode.o ipc/util.o net/core/skbuff.o
            drivers/base/core.o kernel/printk/printk.o kernel/time/timer.o"

stage_k2 () {
	ensure_x86_config
	say "K2: compiling selected translation units from the gate subsystems"
	# Remove them first: a target that is already up to date is not evidence
	# that crugc compiled it, and the stage must not depend on K5's leftovers.
	for o in $k2_objects; do rm -f "$KO/$o"; done
	# shellcheck disable=SC2086
	if ! x86_make $k2_objects >"$STAGES/k2.log" 2>&1; then
		tail -n 40 "$STAGES/k2.log" >&2
		fail "K2: a selected translation unit did not compile"
	fi
	n=0
	for o in $k2_objects; do
		[ -s "$KO/$o" ] || fail "K2: no object at $o"
		n=$((n + 1))
	done
	say "K2: $n translation unit(s) compiled"
}

# -------------------------------------------------------- K3 one subsystem
stage_k3 () {
	ensure_x86_config
	say "K3: building the lib/ subsystem as a whole"
	rm -f "$KO/lib/lib.a" "$KO/lib/built-in.a"
	if ! x86_make lib/ >"$STAGES/k3.log" 2>&1; then
		tail -n 40 "$STAGES/k3.log" >&2
		fail "K3: lib/ did not build"
	fi
	[ -s "$KO/lib/lib.a" ] || fail "K3: lib/ built without lib.a"
	say "K3: lib/ built (lib.a present)"
}

# ------------------------------------------------------- K4 object corpus
corpus_list () {
	awk '/^<!-- corpus:begin -->[ \t]*$/ {on = 1; next}
	     /^<!-- corpus:end -->[ \t]*$/   {on = 0; next}
	     on && $0 ~ /^[A-Za-z0-9_.\/-]+\.o$/ {print}' "$CORPUS"
}

stage_k4 () {
	[ -f "$CORPUS" ] || die "missing $CORPUS (the phase-19 pinned corpus)"
	ensure_x86_config
	corpus_list >"$STAGES/corpus.txt"
	total=$(wc -l <"$STAGES/corpus.txt" | tr -d ' ')
	[ "$total" -gt 0 ] || fail "K4: $CORPUS has no object list between the corpus markers"
	say "K4: compiling $total pinned corpus object(s)"
	while IFS= read -r o; do
		rm -f "$KO/$o"
	done <"$STAGES/corpus.txt"
	# A command line of ~120 objects stays well under the system limit.
	if ! xargs -n 120 make -C "$KSRC" O="$KO" -j"$JOBS" CC="$CRUGC" \
			<"$STAGES/corpus.txt" >"$STAGES/k4.log" 2>&1; then
		tail -n 40 "$STAGES/k4.log" >&2
		fail "K4: a pinned corpus object did not compile"
	fi
	bad=0
	while IFS= read -r o; do
		if [ ! -s "$KO/$o" ]; then
			warn "K4: missing $o"
			bad=1
		elif ! readelf -h "$KO/$o" 2>/dev/null | grep -q 'X86-64'; then
			warn "K4: $o is not an x86-64 object"
			bad=1
		fi
	done <"$STAGES/corpus.txt"
	[ "$bad" -eq 0 ] || fail "K4: the corpus did not compile completely"
	say "K4: $total corpus object(s) compiled"
}

# --------------------------------------------------------- K5 full build
stage_k5 () {
	ensure_x86_config
	say "K5: full kernel build with CC=$CRUGC"
	if ! x86_make >"$STAGES/k5.log" 2>&1; then
		tail -n 40 "$STAGES/k5.log" >&2
		fail "K5: the full build failed"
	fi
	for f in vmlinux arch/x86/boot/bzImage System.map; do
		[ -s "$KO/$f" ] || fail "K5: the build did not produce $f"
	done
	errors=$(grep -c 'error:' "$STAGES/k5.log" 2>/dev/null || true)
	objects=$(find "$KO" -name '*.o' -type f | wc -l | tr -d ' ')
	say "K5: full build ok: $objects object(s), $errors error line(s), vmlinux $(wc -c <"$KO/vmlinux" | tr -d ' ') bytes"
}

# ---------------------------------------------------------- K6 boot smoke
stage_k6 () {
	command -v qemu-system-x86_64 >/dev/null 2>&1 \
		|| die "qemu-system-x86_64 not found (stage K6 needs it)"
	[ -s "$KO/arch/x86/boot/bzImage" ] \
		|| fail "K6 needs the K5 bzImage; run scripts/kernel-stage.sh K5 first"
	[ -f "$ROOT/kernel/boot/init.c" ] || die "missing kernel/boot/init.c"

	boot=$STAGES/k6
	rm -rf "$boot"
	mkdir -p "$boot" || die "cannot create $boot"
	say "K6: building the freestanding /init and an initramfs"
	if ! "$CRUGC" -O2 -static -nostdlib -fno-pie -no-pie \
			-o "$boot/init" "$ROOT/kernel/boot/init.c" \
			>"$boot/init.log" 2>&1; then
		tail -n 20 "$boot/init.log" >&2
		fail "K6: kernel/boot/init.c did not build"
	fi
	# gen_init_cpio is a host program shipped with the kernel; building it
	# directly avoids depending on a configured initramfs in the tree.
	${HOSTCC:-cc} -O2 -o "$boot/gen_init_cpio" "$KSRC/usr/gen_init_cpio.c" \
		>"$boot/gen.log" 2>&1 \
		|| { tail -n 20 "$boot/gen.log" >&2; fail "K6: cannot build usr/gen_init_cpio.c"; }
	{
		printf 'dir /dev 0755 0 0\n'
		printf 'nod /dev/console 0600 0 0 c 5 1\n'
		printf 'file /init %s/init 0755 0 0\n' "$boot"
	} >"$boot/spec"
	"$boot/gen_init_cpio" "$boot/spec" >"$boot/initramfs.cpio" \
		|| fail "K6: cannot create the initramfs"

	say "K6: booting with qemu-system-x86_64 (2 GiB, 2 CPUs, serial console)"
	timeout 180 qemu-system-x86_64 -m 2G -smp 2 \
		-kernel "$KO/arch/x86/boot/bzImage" \
		-initrd "$boot/initramfs.cpio" \
		-append "console=ttyS0 panic=-1 rdinit=/init" \
		-nographic -no-reboot </dev/null >"$boot/console.log" 2>&1
	rc=$?
	grep -q 'Linux version 6\.18\.57' "$boot/console.log" \
		|| { tail -n 20 "$boot/console.log" >&2
		     fail "K6: the kernel did not print the $KERNEL_VERSION banner (qemu rc=$rc)"; }
	grep -q 'Run /init as init process' "$boot/console.log" \
		|| { tail -n 20 "$boot/console.log" >&2
		     fail "K6: the kernel did not start /init (qemu rc=$rc)"; }
	marker=$(grep -c 'crug-boot-ok' "$boot/console.log" || true)
	[ "$marker" = 1 ] \
		|| { tail -n 20 "$boot/console.log" >&2
		     fail "K6: crug-boot-ok appears $marker time(s), want exactly 1"; }
	[ "$rc" -eq 0 ] \
		|| fail "K6: /init printed the marker but qemu did not exit cleanly (rc=$rc)"
	say "K6: boot smoke ok: $KERNEL_VERSION banner, /init ran, crug-boot-ok once, clean poweroff"
}

# ------------------------------------------------------ K7 second architecture
cross_build=$ROOT/build/cross-aarch64
cross_deps=$ROOT/build/deps-cross

# The archived cross binutils link against per-target bfd/opcodes libraries
# (libopcodes-2.46-arm64.so) that no host package provides; the extraction
# places them under usr/lib/x86_64-linux-gnu, so both PATH and LD_LIBRARY_PATH
# must point into build/deps-cross.
use_cross_binutils () {
	PATH=$cross_deps/usr/bin:$PATH
	LD_LIBRARY_PATH=$cross_deps/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
	export PATH LD_LIBRARY_PATH
}

ensure_aarch64_binutils () {
	if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
		return 0
	fi
	if [ -x "$cross_deps/usr/bin/aarch64-linux-gnu-as" ]; then
		use_cross_binutils
		return 0
	fi
	command -v apt-get >/dev/null 2>&1 || die "K7 needs aarch64-linux-gnu-as (no apt-get to fetch it)"
	mkdir -p "$cross_deps/deb" || die "cannot create $cross_deps"
	say "K7: fetching binutils-aarch64-linux-gnu (root-free, build/deps-cross)"
	( cd "$cross_deps/deb" &&
	  apt-get download binutils-aarch64-linux-gnu >"$STAGES/k7-apt.log" 2>&1 ) \
		|| { tail -n 20 "$STAGES/k7-apt.log" >&2
		     die "K7: cannot download binutils-aarch64-linux-gnu"; }
	dpkg -x "$cross_deps/deb"/binutils-aarch64-linux-gnu_*.deb "$cross_deps" \
		|| die "K7: cannot extract the aarch64 binutils"
	use_cross_binutils
	command -v aarch64-linux-gnu-as >/dev/null 2>&1 \
		|| die "K7: aarch64-linux-gnu-as still missing after extraction"
}

ensure_cross_compiler () {
	ensure_aarch64_binutils
	mkdir -p "$cross_build" || die "cannot create $cross_build"
	if [ ! -f "$cross_build/Makefile" ]; then
		say "K7: configuring a cross compiler from this tree (one time)"
		( cd "$cross_build" && "$ROOT/configure" \
			--target=aarch64-linux-gnu --enable-languages=c \
			--disable-bootstrap --disable-multilib --without-headers \
			--disable-libssp --disable-libgomp --disable-libatomic \
			--disable-libquadmath --disable-libstdcxx --disable-nls \
			>"$STAGES/k7-configure.log" 2>&1 ) \
			|| { tail -n 30 "$STAGES/k7-configure.log" >&2
			     fail "K7: configuring the aarch64 cross compiler failed"; }
	fi
	say "K7: building the cross compiler (incremental after the first run)"
	if ! make -C "$cross_build" -j"$JOBS" all-gcc >"$STAGES/k7-cross.log" 2>&1; then
		tail -n 30 "$STAGES/k7-cross.log" >&2
		fail "K7: the aarch64 cross compiler failed to build"
	fi
	[ -x "$CROSS" ] || fail "K7: no cross driver at $CROSS after the build"
	"$CROSS" -dumpversion 2>/dev/null | grep -q '^16\.2' \
		|| die "K7: $CROSS is not a GCC 16.2 cross compiler (KERNEL-BASELINE.md section 5)"
}

arm64_corpus_list () {
	awk '/^<!-- corpus-arm64:begin -->[ \t]*$/ {on = 1; next}
	     /^<!-- corpus-arm64:end -->[ \t]*$/   {on = 0; next}
	     on && $0 ~ /^[A-Za-z0-9_.\/-]+\.o$/ {print}' "$CORPUS"
}

stage_k7 () {
	[ -f "$CORPUS" ] || die "missing $CORPUS"
	arm64_corpus_list >"$STAGES/corpus-arm64.txt"
	total=$(wc -l <"$STAGES/corpus-arm64.txt" | tr -d ' ')
	[ "$total" -gt 0 ] || fail "K7: $CORPUS has no aarch64 object list"

	ensure_cross_compiler
	# The launcher pins the real driver through CRUGC_REAL_DRIVER and execs it
	# verbatim, so the wrapper supplies -B for the uninstalled cross driver
	# (the launcher's in-tree branches add -B themselves).
	cat >"$STAGES/k7-driver" <<-EOF
	#!/bin/sh
	exec "$CROSS" -B"$cross_build/gcc/" "\$@"
	EOF
	chmod +x "$STAGES/k7-driver"

	arm_make () {
		CRUGC_REAL_DRIVER=$STAGES/k7-driver \
		make -C "$KSRC" O="$KO_ARM" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
			-j"$JOBS" CC="$CRUGC" "$@"
	}
	if [ ! -f "$KO_ARM/.config" ]; then
		mkdir -p "$KO_ARM" || die "cannot create $KO_ARM"
		say "K7: configuring: defconfig with ARCH=arm64 in $KO_ARM"
		arm_make defconfig >"$STAGES/k7-defconfig.log" 2>&1 \
			|| { tail -n 20 "$STAGES/k7-defconfig.log" >&2
			     fail "K7: the arm64 defconfig failed"; }
	fi
	say "K7: compiling $total pinned aarch64 object(s)"
	# shellcheck disable=SC2046
	if ! arm_make $(cat "$STAGES/corpus-arm64.txt") >"$STAGES/k7-build.log" 2>&1; then
		tail -n 40 "$STAGES/k7-build.log" >&2
		fail "K7: an aarch64 object did not compile"
	fi
	bad=0
	while IFS= read -r o; do
		if [ ! -s "$KO_ARM/$o" ]; then
			warn "K7: missing $o"
			bad=1
		elif ! readelf -h "$KO_ARM/$o" 2>/dev/null | grep -q 'AArch64'; then
			warn "K7: $o is not an AArch64 object"
			bad=1
		fi
	done <"$STAGES/corpus-arm64.txt"
	[ "$bad" -eq 0 ] || fail "K7: the aarch64 subset did not compile completely"
	say "K7: $total aarch64 object(s) compiled with $(basename "$CROSS")"
}

# ------------------------------------------------------------------- driver
run_stage () {
	case $1 in
	K1) stage_k1 ;;
	K2) stage_k2 ;;
	K3) stage_k3 ;;
	K4) stage_k4 ;;
	K5) stage_k5 ;;
	K6) stage_k6 ;;
	K7) stage_k7 ;;
	esac
}

case $stage in
all)
	check_tree
	verify_pin
	for s in K1 K2 K3 K4 K5 K6 K7; do
		run_stage "$s" || exit $?
	done
	;;
*)
	check_tree
	verify_pin
	run_stage "$stage"
	;;
esac
