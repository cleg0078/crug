# Makefile - the Crug runtime library.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# Phase 13 built the PAL contract; Phase 14 the task runtime and the x86-64
# System V context switch; Phase 18 makes the same core run on Windows x86-64,
# macOS (Darwin) and Linux AArch64 (ADR 0012).  The host is detected, and the
# backend, the architecture and the compiler can be overridden:
#
#   make -C libcrug check-PAL                (this host's backend)
#   make -C libcrug check-PAL PAL=fake       (no operating system at all)
#   make -C libcrug check-PAL PAL=linux      (POSIX threads, futex, mmap, clock)
#   make -C libcrug check-GOROUTINES         (spawn/yield/completion/shutdown)
#   make -C libcrug check-MULTI_OS           (contract + task + channel suites)
#   make -C libcrug check-MULTI_OS TARGET=aarch64-linux
#                                            (Linux AArch64 under qemu)
#   make -C libcrug check-sanitizer          (the Linux suite under ASan/UBSan)
#
# The portability lint keeps libcrug/core free of operating-system primitives
# (ADR 0011 D1, ADR 0012 D2): every platform name lives under pal/ and arch/.

PAL ?=
ARCH ?=
TARGET ?=
CC ?= cc
CROSS_COMPILE ?= aarch64-linux-gnu-
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror

HOST_OS := $(shell uname -s 2>/dev/null || echo unknown)
HOST_ARCH := $(shell uname -m 2>/dev/null || echo unknown)
RUN :=

ifeq ($(TARGET),aarch64-linux)
CC := $(CROSS_COMPILE)gcc
PAL := linux
ARCH := aarch64
QEMU := $(or $(shell command -v qemu-aarch64 2>/dev/null),$(shell command -v qemu-aarch64-static 2>/dev/null))
ifneq ($(QEMU),)
RUN := $(QEMU) -L /usr/aarch64-linux-gnu
endif
BUILD := build/linux-aarch64
else
ifeq ($(PAL),)
  ifeq ($(HOST_OS),Linux)
    PAL := linux
  else ifeq ($(HOST_OS),Darwin)
    PAL := darwin
  else ifneq (,$(findstring MINGW,$(HOST_OS)))
    PAL := windows
  else ifneq (,$(findstring MSYS,$(HOST_OS)))
    PAL := windows
  else ifneq (,$(findstring CYGWIN,$(HOST_OS)))
    PAL := windows
  else ifeq ($(HOST_OS),Windows_NT)
    PAL := windows
  else
    PAL := fake
  endif
endif
BUILD := build/$(PAL)
endif

ifeq ($(ARCH),)
  ifeq ($(HOST_ARCH),x86_64)
    ifeq ($(PAL),windows)
      ARCH := x86_64/windows
    else
      ARCH := x86_64/sysv
    endif
  else ifeq ($(HOST_ARCH),amd64)
    ifeq ($(PAL),windows)
      ARCH := x86_64/windows
    else
      ARCH := x86_64/sysv
    endif
  else ifneq (,$(filter aarch64 arm64,$(HOST_ARCH)))
    ARCH := aarch64
  else
    ARCH := x86_64/sysv
  endif
endif

CPPFLAGS += -Ipal/include -Icore -Iarch/$(ARCH) -DCRUG_PAL_BACKEND='"$(PAL)"'

CORE_OBJ := $(BUILD)/pal_contract.o $(BUILD)/task.o $(BUILD)/chan.o
ARCH_OBJ := $(BUILD)/switch.o $(BUILD)/stack.o
PAL_SRC := pal/$(PAL)/pal_$(PAL).c
PAL_OBJ := $(BUILD)/pal_$(PAL).o
CONTRACT := $(BUILD)/pal-contract
TASKS := $(BUILD)/tasks-test
STRESS := ../build/tasks-stress
BENCH := ../build/tasks-alloc-bench
CHAN_TESTS := $(BUILD)/chan-basic $(BUILD)/chan-handoff $(BUILD)/chan-select
LOG := build/check-pal-$(PAL).log

# Runtime regressions for the scheduler and the channel handoff.  The common
# programs need a working scheduler (the Linux backend is the one the suites
# use); the fake backend adds the deterministic join-failure and
# spawn-during-shutdown cases, and the Linux-only case needs two real threads.
RUNTIME_TESTS := $(BUILD)/chan-ticket $(BUILD)/chan-claim $(BUILD)/recv-scratch \
	$(BUILD)/shutdown-status $(BUILD)/frame-alignment $(BUILD)/cache-restart
RUNTIME_ABORT_TESTS := $(BUILD)/frame-overflow
ifeq ($(PAL),linux)
RUNTIME_TESTS += $(BUILD)/shutdown-concurrent $(BUILD)/burst-stacks
endif
ifeq ($(PAL),fake)
RUNTIME_TESTS += $(BUILD)/join-failure
RUNTIME_ABORT_TESTS += $(BUILD)/spawn-shutdown
endif

PAL_LDLIBS :=
CHECK_EXTRA :=
ifeq ($(PAL),linux)
PAL_LDLIBS := -pthread
endif
ifeq ($(PAL),darwin)
PAL_LDLIBS := -pthread
endif
ifeq ($(PAL),linux)
ifeq ($(ARCH),x86_64/sysv)
CHECK_EXTRA := $(BUILD)/pal-guard-fault
endif
endif

.PHONY: all check check-PAL check-GOROUTINES check-MULTI_OS check-RUNTIME check-tasks bench-tasks check-sanitizer lint clean
all: check-PAL

$(BUILD):
	@mkdir -p $(BUILD)

$(CORE_OBJ): $(BUILD)/%.o: core/%.c core/task.h core/task-internal.h core/chan.h core/pal_contract.h pal/include/crug_pal.h arch/$(ARCH)/ctx.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(PAL_OBJ): $(PAL_SRC) pal/include/crug_pal.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD)/switch.o: arch/$(ARCH)/switch.S | $(BUILD)
	$(CC) $(CPPFLAGS) -c -o $@ $<

$(BUILD)/stack.o: arch/$(ARCH)/stack.c arch/$(ARCH)/ctx.h | $(BUILD)
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

$(BENCH): tests/runtime/alloc_bench.c core/task.h pal/include/crug_pal.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	@mkdir -p ../build
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/alloc_bench.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/chan-basic: tests/runtime/chan_basic.c core/chan.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/chan_basic.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/chan-handoff: tests/runtime/chan_handoff.c core/chan.h core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/chan_handoff.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/chan-select: tests/runtime/chan_select.c core/chan.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/chan_select.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)


$(BUILD)/chan-ticket: tests/runtime/chan_ticket.c core/chan.h core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/chan_ticket.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/chan-claim: tests/runtime/chan_claim.c core/chan.h core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/chan_claim.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/recv-scratch: tests/runtime/recv_scratch.c core/chan.h core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/recv_scratch.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/shutdown-status: tests/runtime/shutdown_status.c core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/shutdown_status.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/frame-alignment: tests/runtime/frame_alignment.c core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/frame_alignment.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/cache-restart: tests/runtime/cache_restart.c core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/cache_restart.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/burst-stacks: tests/runtime/burst_stacks.c core/chan.h core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/burst_stacks.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/shutdown-concurrent: tests/runtime/shutdown_concurrent.c core/task.h pal/include/crug_pal.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/shutdown_concurrent.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/join-failure: tests/runtime/join_failure.c core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/join_failure.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/spawn-shutdown: tests/runtime/spawn_shutdown.c core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/spawn_shutdown.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

$(BUILD)/frame-overflow: tests/runtime/frame_overflow.c core/task.h $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/runtime/frame_overflow.c $(CORE_OBJ) $(ARCH_OBJ) $(PAL_OBJ) $(PAL_LDLIBS)

check-PAL: $(CONTRACT) $(CHECK_EXTRA)
	@mkdir -p build
	@$(RUN) $(CONTRACT) > $(LOG) 2>&1; rc=$$?; cat $(LOG); exit $$rc
ifeq ($(PAL),linux)
ifeq ($(ARCH),x86_64/sysv)
	@$(RUN) $(BUILD)/pal-guard-fault > build/check-pal-$(PAL)-guard.log 2>&1; rc=$$?; cat build/check-pal-$(PAL)-guard.log; exit $$rc
endif
endif

# The task runtime needs real worker threads, so it always runs on the Linux
# backend even when the default PAL of the file is the fake one.
check-GOROUTINES:
	$(MAKE) PAL=linux check-tasks

bench-tasks: $(BENCH)
	@$(RUN) $(BENCH)

check-tasks: $(TASKS) $(STRESS) check-RUNTIME
	@mkdir -p build
	@$(RUN) $(TASKS) > build/check-tasks.log 2>&1; rc=$$?; cat build/check-tasks.log; exit $$rc
	@GOROUTINES_STRESS=1 $(RUN) $(STRESS) > build/check-tasks-stress.log 2>&1; rc=$$?; cat build/check-tasks-stress.log; exit $$rc

# The runtime regressions: ordinary programs must exit 0 with their own FAIL
# diagnostics; the fatal programs must die with the documented message and a
# non-zero status.  $(RUN) covers a cross-runner, $(LOG) stays per backend.
check-RUNTIME: $(RUNTIME_TESTS) $(RUNTIME_ABORT_TESTS)
	@mkdir -p build
	@for t in $(RUNTIME_TESTS); do \
	  log=build/runtime-$(PAL)-$$(basename $$t).log; \
	  $(RUN) $$t > $$log 2>&1; rc=$$?; cat $$log; \
	  if [ $$rc -ne 0 ]; then echo "check-runtime: $$t failed"; exit 1; fi; \
	done
	@for t in $(RUNTIME_ABORT_TESTS); do \
	  log=build/runtime-$(PAL)-$$(basename $$t).log; \
	  if $(RUN) $$t > $$log 2>&1; then \
	    echo "check-runtime: $$t unexpectedly succeeded"; cat $$log; exit 1; \
	  fi; \
	  cat $$log; \
	  msg='crug-CONC-028'; \
	  case $$t in *spawn-shutdown) msg='cannot spawn during shutdown';; esac; \
	  if ! grep -q "$$msg" $$log; then \
	    echo "check-runtime: $$t did not print '$$msg'"; exit 1; \
	  fi; \
	done
	@printf 'runtime[%s]: %s programs, %s fatal programs\n' \
	  "$(PAL)" "$(words $(RUNTIME_TESTS))" "$(words $(RUNTIME_ABORT_TESTS))"

# One command that the Phase 18 gate runs on every platform: the PAL contract,
# the task suites and three plain-C channel programs.  The last line names the
# platform, so a CI log tells which port passed without reading the jobs.
check-MULTI_OS: $(CONTRACT) $(TASKS) $(STRESS) $(CHAN_TESTS)
	@mkdir -p build
	@$(RUN) $(CONTRACT) > build/multi-os-pal.log 2>&1; rc=$$?; cat build/multi-os-pal.log; exit $$rc
	@grep -q " 0 failed" build/multi-os-pal.log || { echo "multi-os: the PAL contract failed"; exit 1; }
	@$(RUN) $(TASKS) > build/multi-os-tasks.log 2>&1; rc=$$?; cat build/multi-os-tasks.log; exit $$rc
	@grep -q " 0 failed" build/multi-os-tasks.log || { echo "multi-os: the task suite failed"; exit 1; }
	@$(RUN) $(STRESS) > build/multi-os-stress.log 2>&1; rc=$$?; cat build/multi-os-stress.log; exit $$rc
	@for t in $(CHAN_TESTS); do \
	  $(RUN) $$t > build/multi-os-$$(basename $$t).log 2>&1; rc=$$?; \
	  cat build/multi-os-$$(basename $$t).log; \
	  if [ $$rc -ne 0 ]; then echo "multi-os: $$t failed"; exit 1; fi; \
	done
	@printf 'multi-os: %s/%s pal-contract %s cases, tasks %s checks, %s channel program(s)\n' \
	  "$(HOST_OS)" "$(ARCH)" \
	  "$$(sed -n 's/.*pal-contract\[[^]]*\]: \([0-9][0-9]*\) cases,.*/\1/p' build/multi-os-pal.log | tail -1)" \
	  "$$(sed -n 's/^tasks: \([0-9][0-9]*\) checks,.*/\1/p' build/multi-os-tasks.log)" \
	  "$(words $(CHAN_TESTS))"

check-sanitizer:
	../scripts/check-tasks-sanitizer.sh

check:
	$(MAKE) check-PAL PAL=fake
	$(MAKE) check-PAL PAL=linux
	$(MAKE) check-GOROUTINES
	$(MAKE) check-RUNTIME PAL=fake
	$(MAKE) check-RUNTIME PAL=linux
	$(MAKE) lint

lint:
	../scripts/lint-no-os-apis.sh libcrug/core

clean:
	rm -rf build ../build/tasks-stress
