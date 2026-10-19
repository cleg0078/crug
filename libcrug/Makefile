# Makefile - the Crug runtime library, contract-check slice.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# Phase 13 builds only the PAL contract and its two backends:
#
#   make -C libcrug check-PAL PAL=fake     (no operating system at all)
#   make -C libcrug check-PAL PAL=linux    (POSIX threads, futex, mmap, clock)
#   make -C libcrug check                  (both, then the portability lint)
#
# The core object list is deliberately tiny: Phase 14 adds the scheduler,
# task, scope and channel sources to CORE_OBJ, and the lint below is what
# keeps them free of system calls.

PAL ?= fake
CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -Ipal/include -Icore -DCRUG_PAL_BACKEND='"$(PAL)"'

BUILD := build/$(PAL)
CORE_OBJ := $(BUILD)/pal_contract.o
PAL_SRC := pal/$(PAL)/pal_$(PAL).c
PAL_OBJ := $(BUILD)/pal_$(PAL).o
CONTRACT := $(BUILD)/pal-contract
LOG := build/check-pal-$(PAL).log

PAL_LDLIBS :=
CHECK_EXTRA :=
ifeq ($(PAL),linux)
PAL_LDLIBS := -pthread
CHECK_EXTRA := $(BUILD)/pal-guard-fault
endif

.PHONY: all check check-PAL lint clean
all: check-PAL

$(BUILD):
	@mkdir -p $(BUILD)

$(CORE_OBJ): core/pal_contract.c core/pal_contract.h pal/include/crug_pal.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(PAL_OBJ): $(PAL_SRC) pal/include/crug_pal.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CONTRACT): tests/pal_contract_main.c $(CORE_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/pal_contract_main.c $(CORE_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/pal-guard-fault: tests/linux/pal_guard_fault.c $(PAL_OBJ) pal/include/crug_pal.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/linux/pal_guard_fault.c $(PAL_OBJ) $(PAL_LDLIBS)

check-PAL: $(CONTRACT) $(CHECK_EXTRA)
	@mkdir -p build
	@$(CONTRACT) > $(LOG) 2>&1; rc=$$?; cat $(LOG); exit $$rc
ifeq ($(PAL),linux)
	@$(BUILD)/pal-guard-fault > build/check-pal-$(PAL)-guard.log 2>&1; rc=$$?; cat build/check-pal-$(PAL)-guard.log; exit $$rc
endif

check:
	$(MAKE) check-PAL PAL=fake
	$(MAKE) check-PAL PAL=linux
	$(MAKE) lint

lint:
	../scripts/lint-no-os-apis.sh libcrug/core

clean:
	rm -rf build
