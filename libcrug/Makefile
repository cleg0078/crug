# Makefile - the Crug runtime library.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# Phase 13 built the PAL contract and its two backends; Phase 14 adds the
# task runtime (core/task.c) and the x86-64 System V context switch under
# arch/.  Two shapes of check:
#
#   make -C libcrug check-PAL PAL=fake     (no operating system at all)
#   make -C libcrug check-PAL PAL=linux    (POSIX threads, futex, mmap, clock)
#   make -C libcrug check-GOROUTINES       (spawn/yield/completion/shutdown)
#   make -C libcrug check-sanitizer        (the same under ASan and UBSan)
#   make -C libcrug check                  (PAL contract, portability lint)
#
# The core object list stays small on purpose: the portability lint below is
# what keeps libcrug/core free of operating-system primitives (ADR 0011 D1),
# and Phase 15+ will add scope and channel sources to it, not new backends.

PAL ?= fake
CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -Ipal/include -Icore -Iarch/x86_64/sysv -DCRUG_PAL_BACKEND='"$(PAL)"'

BUILD := build/$(PAL)
ARCH := arch/x86_64/sysv
CORE_OBJ := $(BUILD)/pal_contract.o $(BUILD)/task.o $(BUILD)/chan.o
ARCH_OBJ := $(BUILD)/switch.o $(BUILD)/stack.o
PAL_SRC := pal/$(PAL)/pal_$(PAL).c
PAL_OBJ := $(BUILD)/pal_$(PAL).o
CONTRACT := $(BUILD)/pal-contract
TASKS := $(BUILD)/tasks-test
STRESS := ../build/tasks-stress
LOG := build/check-pal-$(PAL).log

PAL_LDLIBS :=
CHECK_EXTRA :=
ifeq ($(PAL),linux)
PAL_LDLIBS := -pthread
CHECK_EXTRA := $(BUILD)/pal-guard-fault
endif

.PHONY: all check check-PAL check-GOROUTINES check-tasks check-sanitizer lint clean
all: check-PAL

$(BUILD):
	@mkdir -p $(BUILD)

$(CORE_OBJ): $(BUILD)/%.o: core/%.c core/task.h core/chan.h core/pal_contract.h pal/include/crug_pal.h $(ARCH)/ctx.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(PAL_OBJ): $(PAL_SRC) pal/include/crug_pal.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD)/switch.o: $(ARCH)/switch.S | $(BUILD)
	$(CC) $(CPPFLAGS) -c -o $@ $<

$(BUILD)/stack.o: $(ARCH)/stack.c $(ARCH)/ctx.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# The contract binary links the task object too (task.c is in CORE_OBJ), so it
# needs the architecture objects even when the backend is the fake one: the
# switch is arch code, not backend code (ADR 0011 D1, ADR 0013 D1).
$(CONTRACT): tests/pal_contract_main.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/pal_contract_main.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/pal-guard-fault: tests/linux/pal_guard_fault.c $(PAL_OBJ) pal/include/crug_pal.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/linux/pal_guard_fault.c $(PAL_OBJ) $(PAL_LDLIBS)

$(TASKS): tests/tasks_main.c core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/tasks_main.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(STRESS): tests/tasks_stress.c core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	@mkdir -p ../build
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/tasks_stress.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

check-PAL: $(CONTRACT) $(CHECK_EXTRA)
	@mkdir -p build
	@$(CONTRACT) > $(LOG) 2>&1; rc=$$?; cat $(LOG); exit $$rc
ifeq ($(PAL),linux)
	@$(BUILD)/pal-guard-fault > build/check-pal-$(PAL)-guard.log 2>&1; rc=$$?; cat build/check-pal-$(PAL)-guard.log; exit $$rc
endif

# The task runtime needs real worker threads, so it always runs on the Linux
# backend even when the default PAL of the file is the fake one.
check-GOROUTINES:
	$(MAKE) PAL=linux check-tasks

check-tasks: $(TASKS) $(STRESS)
	@mkdir -p build
	@$(TASKS) > build/check-tasks.log 2>&1; rc=$$?; cat build/check-tasks.log; exit $$rc
	@GOROUTINES_STRESS=1 $(STRESS) > build/check-tasks-stress.log 2>&1; rc=$$?; cat build/check-tasks-stress.log; exit $$rc

check-sanitizer:
	../scripts/check-tasks-sanitizer.sh

check:
	$(MAKE) check-PAL PAL=fake
	$(MAKE) check-PAL PAL=linux
	$(MAKE) check-GOROUTINES
	$(MAKE) lint

lint:
	../scripts/lint-no-os-apis.sh libcrug/core

clean:
	rm -rf build ../build/tasks-stress
