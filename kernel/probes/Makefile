# Phase 19 stage K1: header probes.
#
# These translation units include the kernel headers a stage-K3 subsystem and
# the full build depend on, and nothing else. Stage K1 builds this directory as
# an external module against the pinned tree, so every include resolves exactly
# the way it does in the real build (-D__KERNEL__, include order, generated
# headers, compiler flags).
#
# Built by scripts/kernel-stage.sh K1; never installed or loaded.
obj-m := probe-core.o probe-mm.o probe-sched.o probe-fs.o probe-net.o \
         probe-locking.o probe-irq.o probe-driver.o
