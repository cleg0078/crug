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

check: build ## run the GCC testsuite (heavy; RUNTESTFLAGS selects)
	@if ! command -v runtest >/dev/null 2>&1; then scripts/bootstrap-dejagnu.sh; fi
	@. tools/dejagnu.env 2>/dev/null || true; \
	set -o pipefail; \
	$(MAKE) -C $(OBJ_DIR)/gcc check RUNTESTFLAGS='$(RUNTESTFLAGS)' 2>&1 | tee $(OBJ_DIR)/check.log | grep -E '^(Running |FAIL: |XPASS: |ERROR: |# of )' || true

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

crugc: ## print the in-tree driver and its version
	@printf '%s\n' 'gcc/crugc'
	@gcc/crugc --version | head -n 1

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
	check-C_COMPAT crugc \
	status print-config reconfigure clean distclean