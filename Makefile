# CRUG - top-level build entry point.
#
# CRUG is a fork of GCC 16.2 (docs/crug/GCC-BASELINE.md pins the tarball and
# its SHA512). This Makefile exists so that one command reproduces the pinned
# baseline without remembering paths:
#
#   make baseline      download, verify, extract, fetch GCC's build
#                      prerequisites, configure out of tree, build all-gcc
#   make help          every target
#
# It is deliberately thin. There is no second build system and no custom
# recursion: the real work is upstream's configure and make, driven from
# build/obj. When the CRUG frontend lands, extend the LANGUAGES variable and
# add targets at the bottom (crugc, check-crug, libcrug) rather than forking
# this file.
#
# Layout:
#   gcc/                        the fork source tree, in the repository
#   sources/                    downloaded tarball and checksum-verified copy
#   build/gcc-16.2.0/           unpacked tarball (fallback when gcc/ is absent)
#   build/obj/                  out-of-tree configure/build directory
#   build/.stamps/              markers for the expensive one-time steps
#   $(PREFIX)                   installation prefix for `make install`

SHELL := bash
.DEFAULT_GOAL := help
.NOTPARALLEL:

# ---------------------------------------------------------------- upstream --
GCC_VERSION     ?= 16.2.0
GCC_TARBALL     ?= gcc-$(GCC_VERSION).tar.xz
GCC_URL         ?= https://gcc.gnu.org/pub/gcc/releases/gcc-$(GCC_VERSION)/$(GCC_TARBALL)
GCC_SHA512_FILE ?= docs/crug/gcc-baseline.sha512

# ------------------------------------------------------------------ layout --
SOURCES_DIR ?= sources
BUILD_DIR   ?= build
GCC_SRC     ?= $(if $(wildcard configure),.,$(BUILD_DIR)/gcc-$(GCC_VERSION))
OBJ_DIR     := $(BUILD_DIR)/obj
STAMP_DIR   := $(BUILD_DIR)/.stamps
TARBALL     := $(SOURCES_DIR)/$(GCC_TARBALL)

# The source tree is the in-repository fork when the tree has been imported
# (commit "crug: gcc: import the pinned GCC 16.2.0 tree as the fork base"),
# otherwise the unpacked tarball. When imported, the repository root is the
# GCC top-level directory and gcc/ holds the compiler proper; configure is
# still run out of tree from build/obj. The extract target is a no-op in that
# case, so every target below has one code path.

# -------------------------------------------------------------- host policy --
PREFIX    ?= /opt/crug
JOBS      ?= $(shell nproc 2>/dev/null || echo 4)
LANGUAGES ?= c,c++
ARCH      ?= x86-64
TUNE      ?= generic
DESTDIR   ?=

# Changing any of these needs `make reconfigure`: the stamps do not encode
# configure arguments, because re-running configure on every invocation would
# cost more than the mistake it prevents.
CONFIGURE_FLAGS ?=
CONFIGURE_ARGS := --prefix=$(PREFIX) \
	--enable-languages=$(LANGUAGES) \
	--disable-bootstrap \
	--disable-multilib \
	--enable-checking=yes \
	--with-arch=$(ARCH) --with-tune=$(TUNE) \
	$(CONFIGURE_FLAGS)

RUNTESTFLAGS ?=

# ------------------------------------------------------------------ checks --
REQUIRED_TOOLS := make g++ gcc tar xz curl sha512sum python3 flex bison

# =================================================================== targets ==
##@ Getting started

help: ## show this help
	@printf '%s\n' 'CRUG build targets:'
	@awk 'BEGIN {FS = ":.*##"} /^[a-zA-Z0-9_.-]+:.*##/ {printf "  %-18s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

check-host: ## verify the host has the tools GCC needs (read-only)
	@fail=0; for tool in $(REQUIRED_TOOLS); do \
	  if command -v $$tool >/dev/null 2>&1; then printf '  %-10s %s\n' "$$tool" "$$(command -v $$tool)"; \
	  else printf '  %-10s MISSING\n' "$$tool"; fail=1; fi; \
	done; \
	if ! echo 'int main(){return 0;}' | g++ -std=c++14 -x c++ - -o /dev/null 2>/dev/null; then \
	  echo '  C++14  MISSING (need a C++14 host compiler)'; fail=1; \
	else echo "  C++14  ok ($$(g++ -dumpfullversion -dumpversion))"; fi; \
	avail=$$(df -Pk . | awk 'NR==2 {print $$4}'); \
	if [ "$$avail" -lt 8000000 ]; then echo "  disk    WARNING: $$((avail/1024)) MiB free; a full build wants ~10 GiB"; fi; \
	command -v runtest >/dev/null 2>&1 || echo '  note    dejagnu (runtest) not found: run make bootstrap-dejagnu (no root needed)'; \
	exit $$fail

##@ Pinned GCC baseline

download: $(TARBALL) ## fetch the pinned tarball into sources/
$(TARBALL):
	@mkdir -p $(SOURCES_DIR)
	@touch $@.part
	curl --fail --location --retry 3 --continue-at - --no-progress-meter \
		--output $@.part $(GCC_URL)
	@mv $@.part $@
	@printf 'downloaded %s (%s bytes)\n' $@ "$$(stat -c %s $@)"

verify: $(TARBALL) ## check the tarball against docs/crug/gcc-baseline.sha512
	@test -s $(GCC_SHA512_FILE) || { echo 'missing $(GCC_SHA512_FILE)' >&2; exit 2; }
	cd $(SOURCES_DIR) && sha512sum --check --strict $(abspath $(GCC_SHA512_FILE))

extract: $(STAMP_DIR)/extracted ## unpack the tarball (no-op when gcc/ is the source)
$(STAMP_DIR)/extracted: $(TARBALL) | verify
	@mkdir -p $(BUILD_DIR) $(STAMP_DIR)
	@if [ -f configure ]; then \
	  echo 'using the in-repository fork tree (top-level configure); nothing to extract'; \
	else \
	  tar --extract --xz --file=$(TARBALL) --directory=$(BUILD_DIR); \
	fi
	@test -x $(GCC_SRC)/configure || { echo '$(GCC_SRC)/configure missing after extract' >&2; exit 2; }
	@touch $@

deps: $(STAMP_DIR)/prerequisites ## fetch GMP/MPFR/MPC/ISL into the source tree
$(STAMP_DIR)/prerequisites: $(STAMP_DIR)/extracted
	cd $(GCC_SRC) && ./contrib/download_prerequisites
	@touch $@

configure: $(STAMP_DIR)/configured ## run upstream configure out of tree
$(STAMP_DIR)/configured: $(STAMP_DIR)/prerequisites
	@mkdir -p $(OBJ_DIR) $(STAMP_DIR)
	cd $(OBJ_DIR) && $(CURDIR)/$(GCC_SRC)/configure $(CONFIGURE_ARGS)
	@printf '%s\n' $(CONFIGURE_ARGS) > $(STAMP_DIR)/configure.args
	@touch $@

build: $(STAMP_DIR)/configured ## build the compilers (all-gcc)
	$(MAKE) -C $(OBJ_DIR) -j$(JOBS) all-gcc

build-full: $(STAMP_DIR)/configured ## build everything, including target libraries
	$(MAKE) -C $(OBJ_DIR) -j$(JOBS) all

baseline: download verify extract deps configure build ## the whole pinned baseline, in order
	@printf '\nbaseline ready in %s\n' '$(OBJ_DIR)'
	@$(MAKE) --no-print-directory xgcc-version

xgcc-version: ## print the version of the freshly built xgcc
	@test -x $(OBJ_DIR)/gcc/xgcc || { echo 'xgcc not built yet; run make baseline' >&2; exit 2; }
	@$(OBJ_DIR)/gcc/xgcc -B$(OBJ_DIR)/gcc/ --version | head -n 1

install: build ## install into $(PREFIX) (may need root; DESTDIR is supported)
	$(MAKE) -C $(OBJ_DIR) DESTDIR=$(DESTDIR) install-gcc

##@ Tests

check: build check-P1 ## run the GCC testsuite and the P1 regression set (heavy)
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check RUNTESTFLAGS='$(RUNTESTFLAGS)' 2>&1 | tee $(OBJ_DIR)/check.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

bootstrap-dejagnu: ## install DejaGnu into tools/ without root (no sudo needed)
	scripts/bootstrap-dejagnu.sh

check-c: build ## run the gcc.dg C regression suite
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='$(RUNTESTFLAGS)' 2>&1 | tee $(OBJ_DIR)/check-c.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

baseline-phase0: check-c ## run gcc.dg and archive the result under docs/crug/baseline/phase0/
	scripts/record-baseline.sh phase0 $(OBJ_DIR)/check-c.log

check-C_COMPAT: ## compile the C compatibility corpus with crugc and baseline xgcc
	scripts/check-c-compat.sh

check-KERNEL_COMPAT: ## run the Phase 2 kernel/GNU C compatibility suite (writes out/)
	scripts/test-kernel-compat.sh

check-kernel-objects: ## scan out/*.o for __crug_ symbols and libcrug dependencies
	scripts/check-kernel-objects.sh out

check-CRUG_FLAGS: ## check the -fcrug-* option set, its closure and the feature macros
	scripts/check-crug-flags.sh

check-GNU_C_COMPAT: ## compile the GNU C and kernel corpora with the full Crug flag set
	scripts/check-gnu-c-compat.sh

check-OWNERSHIP: ## run the Crug ownership tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/crug.exp' 2>&1 | tee $(OBJ_DIR)/check-ownership.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-DROP: ## run the Crug drop and destruction tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/drop/drop.exp' 2>&1 | tee $(OBJ_DIR)/check-drop.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-BORROW: ## run the Crug borrow checker tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/borrow/borrow.exp' 2>&1 | tee $(OBJ_DIR)/check-borrow.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-FFI: ## run the Crug FFI contract fixtures (.crugffi)
	@scripts/check-ffi-suite.sh

check-GRAMMAR_DG: ## run the Crug grammar regression tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/grammar/grammar.exp' 2>&1 | tee $(OBJ_DIR)/check-grammar.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-SAFE_TYPES: ## run the Crug safe core type tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/safe-types/safe-types.exp' 2>&1 | tee $(OBJ_DIR)/check-safe-types.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-STRICT_UNSAFE: ## run the Crug strict mode, raw and unsafe tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/strict/strict.exp' 2>&1 | tee $(OBJ_DIR)/check-strict.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-ALIAS_FACTS: ## run the Crug alias-fact (-fcrug-alias-facts) tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/alias/alias.exp' 2>&1 | tee $(OBJ_DIR)/check-alias-facts.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-OPTIMIZATION: check-ALIAS_FACTS ## run the Phase 11 ownership-aware optimization checks
	scripts/check-optimization.sh
	python3 bench/run.py --matrix alias --check
	python3 bench/run.py --matrix alias --lto

check-C2CRUG: check-C2CRUG_PROOFS ## run the Phase 12 c2crug migration checks
	python3 scripts/test-c2crug.py
	scripts/check-c2crug.sh

check-C2CRUG_PROOFS: ## run the Phase 12 c2crug proof cross-checks
	scripts/check-c2crug-proofs.sh

check-P1: ## run the P1 regression set (targeted suites, gate self-test, proofs, runtime)
	scripts/check-p1-regressions.sh

check-GOROUTINES: ## run the Phase 14 goroutine and task checks
	$(MAKE) -C libcrug check-GOROUTINES
	scripts/check-goroutines.sh

check-GOROUTINES_DG: ## run the Crug 'go'/'yield' tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/goroutines/goroutines.exp' 2>&1 | tee $(OBJ_DIR)/check-goroutines-dg.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-SCOPE: ## run the Phase 15 structured-scope checks
	$(MAKE) -C libcrug PAL=linux
	$(MAKE) check-SCOPE_DG
	scripts/check-scope.sh

check-SCOPE_DG: ## run the Crug 'scope' tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/scope/scope.exp' 2>&1 | tee $(OBJ_DIR)/check-scope-dg.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-CHANNELS: ## run the Phase 16 channel and select checks
	$(MAKE) -C libcrug PAL=linux
	$(MAKE) check-CHANNELS_DG
	scripts/check-channels.sh

check-CHANNELS_DG: ## run the Crug channel and select tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/channels/channels.exp' 2>&1 | tee $(OBJ_DIR)/check-channels-dg.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}

check-SEND_SYNC: ## run the Phase 17 Send/Sync checks
	@$(MAKE) --no-print-directory check-SEND_SYNC_DG
	scripts/check-send-sync.sh

check-SEND_SYNC_DG: ## run the Crug Send/Sync tests under DejaGnu
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check-c RUNTESTFLAGS='crug.dg/send-sync/send-sync.exp' 2>&1 | tee $(OBJ_DIR)/check-send-sync-dg.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )'; \
	exit $${PIPESTATUS[0]}


check-PAL: ## run the Phase 13 PAL contract checks on both backends
	$(MAKE) -C libcrug check-PAL PAL=fake
	$(MAKE) -C libcrug check-PAL PAL=linux
	scripts/check-pal.sh
	scripts/lint-no-os-apis.sh libcrug/core

check-MULTI_OS: ## run the Phase 18 multi-OS runtime checks (TARGET=aarch64-linux for AArch64)
	$(MAKE) -C libcrug check-MULTI_OS TARGET=$(TARGET)

check-KERNEL: ## run the Phase 19 kernel stages, the object diff and the P0 ledger check
	scripts/kernel-stage.sh all
	scripts/kernel-object-diff.sh
	scripts/check-p0-open.sh --kernel

check-DIAGNOSTICS: ## check the Phase 20 diagnostic goldens and the SARIF contract
	scripts/check-diagnostic-goldens.sh
	scripts/check-diagnostic-sarif.sh

check-docs-examples: ## compile and run the examples quoted in docs/crug/MIGRATION-EXAMPLES.md
	scripts/check-docs-examples.sh

check-DOCS: ## check the documentation set against docs/crug/REQUIRED-DOC-SECTIONS.md
	scripts/check-docs.sh

##@ Performance

pristine-xgcc: ## build the unmodified GCC 16.2 compiler used for the codegen parity check
	@scripts/build-pristine.sh

check-PERFORMANCE: ## run the Phase 21 performance qualification and write bench/results/<date>.md
	@bench/run.py --performance --check

##@ Security and reliability

check-FUZZ: ## run the Phase 22 fuzz harnesses (FUZZ_BUDGET=<inputs per target>, FUZZ_RUNS=<runtime sequences>)
	scripts/check-fuzz.sh

check-SANITIZER: ## run the Phase 22 ASan/UBSan suite and the TSan channel arm
	scripts/check-sanitizer.sh

fork-delta: ## print the fork delta against the pinned GCC vendor tag
	scripts/fork-delta.sh

check-gcc-drift: ## check the fork's hunks, symbols and pins against upstream GCC
	scripts/check-gcc-drift.sh

verify-upgrade: ## run the upgrade acceptance matrix (ARGS='--build --gates' for the full run)
	scripts/verify-upgrade.sh $(ARGS)

crugc: ## print the in-tree driver and its version
	@printf '%s\n' 'gcc/crugc'
	@gcc/crugc --version | head -n 1

##@ Packaging

package: ## build the release tarball, the Debian package, the checksums and the SPDX SBOM into dist/
	@scripts/package.sh

package-deb: ## build the Debian package dist/crug_<version>-<revision>_amd64.deb
	@scripts/build-deb.sh

package-image: ## build the container image crug/pkg:<version> from packaging/Dockerfile
	@packaging/build-image.sh

check-DEB: ## verify the Debian package layout, dependencies and smoke programs
	@scripts/check-deb.sh

check-launcher: ## verify that bin/crugc finds a Crug driver through a symlink
	@scripts/check-crugc-launcher.sh

check-PACKAGING: ## verify the release artefacts, the SBOM, the image and the Debian package
	@scripts/check-packaging.sh

##@ Release candidate

check-RC: ## run the Phase 25 release-candidate checks (archives, ledger, nightly window, matrix)
	@scripts/check-gate-archives.sh
	@scripts/check-p0-open.sh
	@scripts/count-open-p1.sh --max=2
	@scripts/check-regressions.sh
	@sh scripts/check-fuzz-nightly.sh --window 30
	@$(MAKE) --no-print-directory check-MULTI_OS check-FFI
	@echo 'check-RC: run scripts/kernel-stage.sh K5 and K6, and make check-PERFORMANCE, before tagging'

##@ Examples

# The directory `make examples` reads.  Override it to build any other tree of
# examples with the same command: a file whose basename is one of the examples/
# programs keeps its documented flags, and any other program is compiled with
# no -fcrug-* flag.
EXAMPLES ?= examples

examples: ## build the examples/ programs into build/examples/ (EXAMPLES=<dir> selects another source directory)
	@EXAMPLES_DIR="$(EXAMPLES)" scripts/build-examples.sh

##@ Maintenance

status: ## show where the build stands
	@printf '%-22s %s\n' 'tarball' "$$(test -f $(TARBALL) && echo "present ($$(stat -c %s $(TARBALL)) bytes)" || echo missing)"
	@printf '%-22s %s\n' 'source tree' "$$(if [ '$(GCC_SRC)' = . ]; then echo 'repository root (configure in ., compiler in gcc/)'; elif [ -d $(GCC_SRC) ]; then echo '$(GCC_SRC)'; else echo missing; fi)"
	@printf '%-22s %s\n' 'prerequisites' "$$(test -d $(GCC_SRC)/mpfr && echo fetched || echo 'not fetched')"
	@printf '%-22s %s\n' 'configured' "$$(test -f $(STAMP_DIR)/configured && echo yes || echo no)"
	@printf '%-22s %s\n' 'xgcc' "$$(test -x $(OBJ_DIR)/gcc/xgcc && echo built || echo 'not built')"
	@printf '%-22s %s\n' 'target libs' "$$(test -f $(OBJ_DIR)/x86_64-pc-linux-gnu/libgcc/libgcc.a && echo built || echo 'not built (make build-full)')"
	@printf '%-22s %s\n' 'prefix' '$(PREFIX)'

print-config: ## print the configure invocation (no side effects)
	@printf '%s\n' '$(CONFIGURE_ARGS)'

reconfigure: ## re-run configure from scratch (needed after prefix/flags changes)
	rm -rf $(OBJ_DIR) && rm -f $(STAMP_DIR)/configured
	$(MAKE) --no-print-directory configure

clean: ## remove the build directory, keep sources and the tarball
	rm -rf $(OBJ_DIR) $(STAMP_DIR)

distclean: ## remove build/ and sources/ entirely
	rm -rf $(BUILD_DIR) $(SOURCES_DIR)

##@ Future CRUG targets (placeholders)
#
# crug-frontend: build gcc/crug/ as part of the compiler once it exists.
#                gcc/crugc is the phase-1 launcher; it becomes a driver built
#                with its own specs when Crug needs different defaults.
# check-crug:    run gcc/testsuite/crug.dg/ via the GCC dejagnu harness.
# libcrug:       build the PAL under libcrug/ (its own Makefile).
#
# The eventual shape is `LANGUAGES=c,c++,crug` plus `--enable-crug`, so these
# should stay thin wrappers over $(OBJ_DIR) rather than a parallel build system.

.PHONY: help check-host download verify extract deps configure build build-full \
	baseline xgcc-version install check bootstrap-dejagnu check-c baseline-phase0 \
	check-C_COMPAT check-KERNEL_COMPAT check-kernel-objects crugc \
	check-CRUG_FLAGS check-GNU_C_COMPAT check-DROP check-BORROW \
	check-STRICT_UNSAFE check-FFI check-GRAMMAR_DG check-SAFE_TYPES check-ALIAS_FACTS check-OPTIMIZATION \
	check-C2CRUG check-C2CRUG_PROOFS check-P1 check-PAL check-GOROUTINES check-GOROUTINES_DG \
	check-SCOPE check-SCOPE_DG check-CHANNELS check-CHANNELS_DG \
	check-SEND_SYNC check-SEND_SYNC_DG check-MULTI_OS check-KERNEL examples \
	check-DIAGNOSTICS check-docs-examples check-DOCS pristine-xgcc check-PERFORMANCE \
	check-FUZZ check-SANITIZER package package-deb package-image check-DEB check-launcher \
	check-PACKAGING check-RC \
	fork-delta check-gcc-drift verify-upgrade \
	status print-config reconfigure clean distclean