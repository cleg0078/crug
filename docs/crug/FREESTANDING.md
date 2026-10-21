# Freestanding and kernel builds

A freestanding C program does not assume a hosted C implementation. It may
have no `main` called by a C runtime, no standard library, no constructors
and no operating-system services. Crug supports that setting because the
Linux kernel is its largest compatibility corpus: the compiler must accept
kernel-shaped GNU C, inject no hidden runtime, and leave plain C code
generation alone.

Status: Phase 23 documentation of the Phase 2 and Phase 19 surfaces,
2026-10-20. The compatibility corpus and the no-runtime checks are Phase 2;
the pinned Linux build and boot are Phase 19. The kernel mode decision is
[adr/0017-kernel-ownership-policy.md](adr/0017-kernel-ownership-policy.md) (Proposed); the pinned
tree and evidence are in [KERNEL-BASELINE.md](KERNEL-BASELINE.md) and
[KERNEL-COMPATIBILITY.md](KERNEL-COMPATIBILITY.md).

## 1. What freestanding means here

Freestanding here means the C standard's `__STDC_HOSTED__ == 0` setting,
plus the link-time absence of a hosted startup. The normal invocation adds
`-ffreestanding` and `-nostdlib`:

~~~text
crugc -fcrug-kernel -ffreestanding -nostdlib -O2 -c -o out/name.o source.c
~~~

No single Crug flag implies the other two. `-fcrug-kernel` is a language
mode; `-ffreestanding` selects the C library model; `-nostdlib` keeps the
hosted startup and libraries out of the link. A build that passes only one of
them is not the kernel configuration and should not claim its guarantees.

Freestanding is not a Crug language subset. Plain C remains plain C:
statement expressions, `typeof`, `__auto_type`, attributes, sections,
packed and aligned types, zero-length and flexible arrays, computed goto,
inline assembly, `asm goto`, volatile MMIO, atomics, branch hints and
`container_of` are accepted in the same way as in the GCC baseline. The
kernel corpus in `gcc/testsuite/crug.dg/kernel/` names each construct and
compiles it without a libc header.

The claim is compatibility, not safety. Kernel-mode code gets no ownership
analysis and no drop lowering; it is ordinary GNU C that the compiler accepts
and does not instrument. That boundary is deliberate (ADR 0017).

## 2. Kernel compatibility mode

`-fcrug-kernel` is a mode, not an ignore flag. The driver declares it as a
`Common Var` option, and the front end sets `CRUG_FEAT_KERNEL` when it is
present. The mode defines these macros:

~~~text
__CRUG__=1
__CRUG_COMPILER__=1
__CRUG_KERNEL__=1
~~~

The first two are the ordinary Crug identity macros. `__CRUG_KERNEL__` lets
a source file test the mode without changing its command line. Defining the
macro is the only source-visible effect on a translation unit that does not
use a removed feature.

The mode removes the concurrency surface: `go`, `scope`, `chan`,
`send`, `recv`, `close` and `select` are unavailable. It also refuses
the ownership and borrow extensions. Combining it with
`-fcrug-goroutines`, `-fcrug-channels` or `-fcrug-modern` is a hard error
with the message that the kernel mode removes those features. Combining it
with `-fcrug-ownership` or `-fcrug-borrow` is a hard error that names the
missing kernel-mode drop lowering. The compiler does not silently drop a
feature and does not run the ownership analysis in this mode.

`-fcrug-kernel` does not imply `-ffreestanding` and does not imply
`-nostdlib`. A kernel build passes all three, as the corpus does. A plain C
translation unit that does not test `__CRUG_KERNEL__` produces a
byte-identical object with and without `-fcrug-kernel`; the mode changes no
code generation for the C it accepts. That is the property the object checks
protect.

Kernel code has no `.crugffi` catalogue and no ownership contracts in v1.
There is no mapping from `kmalloc` to `kfree`, no borrow of a kernel
structure and no destructor lowering in this mode. Kernel-mode drop lowering
is future work and is not implemented (ADR 0017, Proposed).

`KERNEL-COMPATIBILITY.md` section 2 still records the Phase 2 state, where
the option was declared `Common Ignore` and had no semantic effect. The
current tree implements the mode described here; where the two pages differ,
the source and the Phase 19 gate are authoritative.

## 3. What the compiler avoids

The kernel profile promises no hidden runtime. The checks inspect the object,
not the source, and reject four things:

- a defined or undefined `__crug_` symbol, which would be an injected helper
  or an unexpected runtime dependency;
- a dynamic-section dependency on `libcrug`;
- a hosted allocation, thread or C++ exception entry point:
  `malloc`, `calloc`, `realloc`, `free`, `pthread_*` or `__cxa_*`;
- a `.init_array` section, because a constructor would run before the
  kernel's start-up code is ready.

The compiler emits no startup file, no constructor, no destructor registration
and no Crug helper call in this mode. `-nostdlib` keeps the hosted startup
and libraries out of the link; the object checks make the compile-only corpus
prove that the translation unit itself does not ask for them.

A source file that calls an ordinary C function still references it. A
structure assignment or a loop may cause the optimizer to emit a `memcpy` or
`memset` call, which the kernel supplies; that is a normal C library call,
not an injected Crug runtime. The checks are aimed at the Crug runtime and the
hosted start-up surface, not at every symbol a kernel object may import.

## 4. Supported targets

The primary target is Linux 6.18.57, x86_64, `defconfig`. The pin is recorded
in [KERNEL-BASELINE.md](KERNEL-BASELINE.md): tag `v6.18.57`, tag object
`c05de972ca192e22ce318dd70ce8e990b9466f6d`, commit
`2ff600d29ee478d0a8d46ef8a58afb5e038fe692`, tree SHA256
`5bd9acdc82ec5efc3790ae840e5aaa40dfde7efff261cf6dde902dc570acf98c`.

Phase 19 measured the tree in stages. K1 compiled eight header probes; K2
compiled twelve selected translation units; K3 built `lib/` as 106 objects;
K4 built 438 objects from nine directories (`arch/x86/mm` 20,
`drivers/base` 29, `fs` 65, `init` 8, `ipc` 11, `kernel` 76,
`lib` 106, `mm` 75, `net/core` 48). 436 of the 438 objects are
byte-identical to the reference build; the two differences are explained in
`kernel/object-diff-allow.txt`: `init/version-timestamp.o` embeds the build time
and build number, and `init/version.o` embeds the compiler identification that
`scripts/mkcompile_h` copies from `$CC --version` (`crugc 0.1.0` for the
launcher, `xgcc (GCC) 16.2.0` for the reference driver).

K5 built the whole tree: 2,980 compiler invocations, zero errors and zero
warnings, `vmlinux` 51,420,536 bytes, `bzImage` 14,783,488 bytes and
`System.map` 8,496,151 bytes. K6 booted that kernel under QEMU 10.2.1 and
observed the `Linux version 6.18.57` marker, `Run /init as init process`,
one `crug-boot-ok` line and a clean poweroff. K7 compiled 15 objects for
AArch64 as a compile-only subset.

AArch64 is not a boot target for Crug v1, and the compiler itself is not
cross-built for AArch64; the runtime has a separate AArch64 port documented in
[PORTABILITY.md](PORTABILITY.md). There is no BSD, RISC-V or Windows kernel
target in v1.

## 5. Limits

- No ownership, borrow or drop analysis in kernel mode. The mode removes those
  features instead of approximating them.
- No kernel ownership contracts, no `kmalloc`/`kfree` mapping and no
  `.crugffi` metadata in v1. A future kernel profile needs a superseding ADR.
- No RCU-aware analysis and no kernel task backend in v1. Kernel
  synchronisation remains ordinary C.
- No AArch64 boot and no second architecture beyond the compile-only K7
  subset. There is no generic bare-metal target, no bootloader support and no
  freestanding libc.
- The pin is Linux 6.18.57 x86_64 only. A newer kernel, a different
  `defconfig` or a downstream patch series is outside the measured claim.
- No performance claim. Phase 21 measures optimisation separately; the kernel
  stages check acceptance and object identity, not speed.
- No kernel module ABI claim. The corpus compiles kernel objects; it does not
  promise that every module or out-of-tree subsystem builds.

The fixed, pinned scope is the point: a compatibility result that moves with
every kernel release cannot be reviewed. `KERNEL-BASELINE.md` section 7
records how the pin is updated.

## 6. Tests

The kernel checks are separate because they answer different questions. The
compatibility runner asks whether each named construct compiles, the object
checks ask what the object contains, and the stage script asks whether the
pinned tree builds and boots.

| Check | Expected result |
| --- | --- |
| `make check-KERNEL_COMPAT` | `kernel-compat: 21/21 work items PASS` |
| `make check-kernel-objects` | `kernel-objects: 20 objects, __crug_=0, libcrug=0` |
| `scripts/check-kernel-freestanding.sh` | `freestanding: no runtime symbols, no init_array` |
| `scripts/kernel-object-diff.sh` | every object identical or explained; 436/438 identical |
| `scripts/check-p0-open.sh --kernel` | no open kernel P0, every fixed P0/P1 has a regression id |
| `scripts/kernel-stage.sh K1` ... `K7` | each stage prints its observable and exits 0 |

`make check-KERNEL_COMPAT` writes `out/` and runs all 21 work items; it
exits 2 when no runnable driver or corpus exists and 1 on a failed item.
`scripts/check-kernel-objects.sh out` checks the 20 objects left by the
freestanding gate for `__crug_` symbols and a `libcrug` dependency. The
freestanding script compiles `freestanding.c` with the three flags and uses
`nm -u` and `readelf -S` for its four rejections.

`scripts/kernel-stage.sh` drives K1 through K7 against the pinned tree and
never modifies it. `scripts/kernel-object-diff.sh` compiles the corpus
through `crugc` and through the wrapped GCC driver and compares the objects
byte for byte. `kernel/KERNEL-CORPUS.md` is the object list those checks
consume.

The CI job is `.github/workflows/kernel-compat.yml`, named
`kernel-compat`. The Phase 19 stage evidence is archived under
`docs/crug/gates/` and summarised in [EXIT-GATES.md](EXIT-GATES.md).
