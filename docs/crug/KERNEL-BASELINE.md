# CRUG kernel baseline

**Document type:** External-input pin (Linux kernel test corpus)
**Status:** Accepted (2026-10-20)
**Owner:** CRUG project owner (compiler lead). Gate adjudicator: a second engineer, or the CI job that runs `scripts/kernel-pin.sh --verify` and the phase-19 checks.
**Derived from:** `spec/CRUG_PRODUCTION_ROADMAP.md` sections 7 and 24 (Phase 2 kernel/GNU-C baseline and Phase 19 stages K0–K7) and section 26 (Phase 21 kernel codegen parity); `spec/CRUG_MASTER_SPEC_EN.md` section 17 (Linux Kernel C Compatibility), sections 41.5–41.6 (external corpus, K0–K7 milestones) and section 83 (pinned kernel test source/corpus); `spec/CRUG_RFC_0001.md` section 7.2 (`class K0`) and section 38 (kernel profile).

This file pins the external Linux kernel source against which kernel compatibility
claims are measured. `docs/crug/GCC-BASELINE.md` section 4 still records the pin as
missing and leaves the open decision tracked as C-09 in `spec/000-PRECEDENCE.md`;
this document supplies the tag, commit, configuration and boot command that section 4
requires. The Phase 19 gate now builds and boots the tree; section 8 records the
result. Pinning the corpus is still not a claim of `class K0` (RFC-0001
section 7.2).

## 1. Pinned artefact

The pin is Linux 6.18.57 from the stable tree.

| Item | Value |
|------|-------|
| Version | 6.18.57 (`longterm`) |
| Tag | `v6.18.57` (annotated; tag object `c05de972ca192e22ce318dd70ce8e990b9466f6d`) |
| Commit | `2ff600d29ee478d0a8d46ef8a58afb5e038fe692` |
| Release date | 2026-10-15 |
| Stable-tree repository | https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git |
| Tarball | https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.57.tar.xz |
| Size | 154,863,520 bytes |
| SHA256 | `5bd9acdc82ec5efc3790ae840e5aaa40dfde7efff261cf6dde902dc570acf98c` |
| Digest file | `docs/crug/kernel-baseline.sha256` |
| Signed checksum source | https://cdn.kernel.org/pub/linux/kernel/v6.x/sha256sums.asc |
| EOL | December 2028 (6.18 longterm series) |

The tag is annotated, so `git ls-remote` reports two objects: the tag object above
and its peeled commit `2ff600d29ee478d0a8d46ef8a58afb5e038fe692`. The commit is what
every stage compiles. A checkout is at the pin only when it resolves to that commit;
a branch, a later point release or a distro patch set is a different corpus.

The digest was computed from the downloaded tarball and compared, as text, with the
`linux-6.18.57.tar.xz` line of the kernel.org `sha256sums.asc`. That file is PGP
clearsigned, but no key for it was available in the verifying keyring, so the
signature itself was not validated; the published digest line was cross-checked
instead. `scripts/kernel-pin.sh --verify` re-checks the local digest line against
`sources/kernel` without network access.

## 2. What the pin governs

- **Kernel compatibility work.** Roadmap section 7 (Phase 2) and roadmap section 24
  (Phase 19). Roadmap section 24 names the stages K0–K7; RFC-0001 sections 7.2 and 54
  use the same ladder for kernel qualification levels and the `class K0` conformance
  class. The token is overloaded, so this documentation set writes `stage K0`…`stage K7`
  for the roadmap ladder and `class K0` for the RFC class
  (`docs/crug/EXIT-GATES.md` section 4).
- **Phase 2 synthetic kernel microtests.** The GNU-C microtests (statement
  expressions, `typeof`, `__auto_type`, attributes, sections, packed/zero-length and
  flexible arrays, computed goto, `asm`, `asm goto`, volatile/MMIO, atomics,
  `container_of`, `-ffreestanding`, `-nostdlib`) live under
  `gcc/testsuite/crug.dg/kernel/` and do **not** need the real tree. The pin is
  informative for them: RFC-0001 section 38.2 fixes the dialect target at the
  documented kernel GNU C dialect (GNU C11, `-std=gnu11`), and this tree is the
  reference that defines it.
- **Phase 19 real kernel build and boot.** Stages K1–K6 (selected headers,
  translation units, subsystem, broad object corpus, full build, boot smoke) compile
  and run the pinned tree. Section 5 fixes the subsystem scope; stage K7 (second
  architecture) is excluded there.
- **Phase 21 codegen parity.** Roadmap section 26 requires kernel object size, hot
  assembly, stack usage and symbol comparison; `docs/crug/EXIT-GATES.md` conflict C3
  folds the master spec's 18K kernel codegen parity into Phase 21. Parity is measured
  on objects built from this tree against a reference GCC build of the same tree.
- **ADR 0017 (kernel ownership policy).** `docs/crug/adr/README.md` records the
  required ADR 0017 as depending on C-09, the kernel pin. What ownership may lower to
  in kernel code is decided against this baseline.
- **Not governed: the conformance claim.** RFC-0001 section 7.2 requires the project
  kernel GNU-C corpus and freestanding compilation without `libcrug` or hidden runtime
  state. This document supplies the pinned source; it does not establish the
  per-object corpus (section 5 does not give object counts), run a check, or claim
  `class K0`.

## 3. Build

The tarball extracts to `build/linux-6.18.57` (its top-level directory is
`linux-6.18.57/`) and is built with an out-of-tree object directory,
`build/kernel-crug`. Both live under the gitignored `/build/`. From the
repository root:

```sh
tar --extract --xz --file=sources/kernel/linux-6.18.57.tar.xz --directory=build
make -C build/linux-6.18.57 O=$PWD/build/kernel-crug x86_64_defconfig
make -C build/linux-6.18.57 O=$PWD/build/kernel-crug -j$(nproc) CC=$PWD/gcc/crugc
```

`O=` must be absolute (`$PWD`), because the kernel build system rejects a relative
object directory. `x86_64_defconfig` resolves to
`arch/x86/configs/x86_64_defconfig` (section 5). The third command is Phase 19
stage K5: it builds with the Crug driver, and `scripts/kernel-stage.sh K5` runs it
and asserts `vmlinux`, `arch/x86/boot/bzImage` and `System.map`. The reference
build for `scripts/kernel-object-diff.sh` uses the same tree and configuration in
`build/kernel-ref` with the GCC driver the launcher wraps (Phase 21 compares
object code against it).

The host must satisfy the kernel's own documented build requirements; this pin
adds none. One requirement is easy to miss on a minimal host: the kernel's
`objtool` needs the libelf headers. This checkout's root-free copy under
`build/deps/usr/include` plus `build/deps/lib` is used through `CPATH` and
`LIBRARY_PATH` by the stage scripts when it exists.

## 4. Boot smoke

Phase 19 stage K6 runs this command (`scripts/kernel-stage.sh K6`), with an
initramfs attached and `rdinit=/init`:

```sh
qemu-system-x86_64 -m 2G -smp 2 \
  -kernel build/kernel-crug/arch/x86/boot/bzImage \
  -initrd build/kernel-stages/k6/initramfs.cpio \
  -append "console=ttyS0 panic=-1 rdinit=/init" -nographic -no-reboot
```

Expected console markers, in order:

- `Linux version 6.18.57` — the kernel banner, once the decompressed kernel starts.
- `Run /init as init process` — printed when the initramfs `/init` is executed.
- `crug-boot-ok` — printed exactly once by the freestanding `/init`
  (`kernel/boot/init.c`), which then calls `reboot (LINUX_REBOOT_CMD_POWER_OFF)`
  so qemu exits instead of hanging.

`x86_64_defconfig` enables the 8250 serial console (`CONFIG_SERIAL_8250_CONSOLE=y`)
and initramfs support (`CONFIG_BLK_DEV_INITRD=y`), so `console=ttyS0` reaches the
terminal and an initramfs can be attached without a config fragment. A clean boot
needs an initramfs carrying a statically linked `/init`: without one the kernel has
no userspace and panics, and `panic=-1` keeps the failure visible on the serial
console instead of rebooting the VM.

The boot was run on 2026-10-20 with QEMU 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.2);
the console showed the three markers above and ended with `reboot: Power down`.
No qemu version is part of the pin; a later boot record names its own.

## 5. Scope and object list

- **Configuration:** `arch/x86/configs/x86_64_defconfig` is the only configuration.
  There is no reduced or extra config fragment; the pinned configuration is the full
  defconfig. A fragment that changes what is compiled is an amendment (section 7).
- **Subsystems expected to compile in the Phase 19 gate:** `init/`, `kernel/`,
  `mm/`, `fs/`, `ipc/`, `net/`, `drivers/base/`, `lib/`. This set is the gate's
  compile scope, and stage K3 ("one subsystem", roadmap section 24) can be met
  inside it.
- **Object list and counts:** `kernel/KERNEL-CORPUS.md` pins the Phase 19
  corpus: 438 x86_64 objects for stage K4 across the nine directories
  `arch/x86/mm`, `drivers/base`, `fs`, `init`, `ipc`, `kernel`, `lib`, `mm`
  and `net/core`, plus 15 aarch64 objects for stage K7. It also fixes stage K3's
  size: the 106 objects directly in `lib/`. The lists are generated from this
  configuration, not hand-picked; a change to them is an amendment to this document
  (amendment A-1).
- **Architecture:** x86_64 is the build and boot architecture. Stage K7 pins the
  second architecture as a compile-only subset: the same tree with Kbuild's arm64
  `defconfig` (`make ARCH=arm64 defconfig`), and a GCC 16.2.0 cross compiler
  configured from this same source tree
  (`--target=aarch64-linux-gnu --without-headers`). No aarch64 boot is part of the
  gate. A further architecture is an amendment (section 7).
- **Out of scope:** kernel ownership contracts (optional per master spec 8K and the
  phase-19 non-goals in `docs/crug/EXIT-GATES.md`), RCU-aware modelling, and a kernel
  task backend. None of these changes the corpus.

## 6. Corpus policy

1. The Linux kernel source is licensed GPLv2. It is not part of this repository: no
   kernel source, patch, defconfig copy or extracted tree is vendored or committed.
   The source lives under `sources/` (ignored by `.gitignore`, `/sources/`) or in an
   external checkout. Only `docs/crug/kernel-baseline.sha256` and this document enter
   the repository.
2. The tarball is verified by digest before every use:
   `scripts/kernel-pin.sh --verify`, equivalently
   `sha256sum --check --strict docs/crug/kernel-baseline.sha256` from
   `sources/kernel`. A tree whose digest fails is not the pin and must not produce a
   build, boot or parity result.
3. The tarball is the artefact the digest covers. An external git checkout is
   acceptable only at the pinned commit `2ff600d29ee478d0a8d46ef8a58afb5e038fe692`
   (tag object `c05de972ca192e22ce318dd70ce8e990b9466f6d`), and any result recorded
   from a checkout names that commit. A later point release, a distro patch set or a
   working branch is a different corpus and needs the update procedure of section 7.
4. `docs/crug/kernel-baseline.sha256` is one line in `sha256sum` format: the digest,
   two spaces, and `linux-6.18.57.tar.xz`. Tools that parse it assume that shape.

## 7. Update policy

1. A point release (for example 6.18.58) changes only by a new ADR or by an amendment
   to this document that quotes the old and new tag, commit and digest. Editing one
   value alone is not an update.
2. Tag, commit and digest move together. The digest file, the URL in
   `scripts/kernel-pin.sh` and the values in section 1 change in one commit.
3. A failed digest check stops the work. Do not build, boot, record a corpus result
   or open a parity claim from an unverified tree; report the mismatch and re-fetch.
4. A change of development series (for example 6.19) is a new pin decision and a new
   ADR, not an amendment. The pin is 6.18.57 or it is a different pin.
5. The rules of `docs/crug/GCC-BASELINE.md` section 6 also apply: quote old and new
   values in the commit message, and re-run any gate archive that the change
   invalidates.

## 8. Evidence

- `docs/crug/kernel-baseline.sha256` — the recorded digest, one line.
- `scripts/kernel-pin.sh --verify` — runs `sha256sum --check --strict` from
  `sources/kernel` and prints `kernel-6.18.57: OK`. Archive its output with any gate
  that consumes the corpus.
- Performed when this document was written (2026-10-18): the tarball was downloaded
  from the pinned URL; its SHA256 was computed locally and compared as text with the
  `linux-6.18.57.tar.xz` line of `sha256sums.asc`; `git ls-remote` returned tag
  object `c05de972ca192e22ce318dd70ce8e990b9466f6d` and commit
  `2ff600d29ee478d0a8d46ef8a58afb5e038fe692`; the defconfig was read from the
  tarball; `qemu-system-x86_64` was present. The `sha256sums.asc` signature was not
  validated — no key for it was available in the verifying keyring — so the digest
  line was cross-checked as text only.
- Performed by the Phase 19 gate on 2026-10-20 (archived under
  `docs/crug/gates/phase-19/`; the run at `3775c34` passed 6/6):
  - `scripts/kernel-stage.sh K1` — 8 header probes compiled as an external module.
  - `K2` — 12 selected translation units; `K3` — `lib/` built as a whole.
  - `K4` — 438 corpus objects compiled from source.
  - `K5` — full build with `CC=gcc/crugc`: 2,980 `CC` lines, 0 errors, 0
    warnings, `vmlinux` 51,420,536 bytes, `bzImage` 14,783,488 bytes,
    `System.map` 8,496,151 bytes.
  - `K6` — boot with QEMU 10.2.1: banner, `Run /init as init process`,
    `crug-boot-ok` once, clean poweroff.
  - `K7` — 15 aarch64 objects compiled with the cross compiler and checked to be
    AArch64 relocatables.
  - `scripts/kernel-object-diff.sh` — 436 of the 438 objects byte-identical to
    the same objects built by the wrapped GCC driver. Two objects embed text that
    cannot match: `init/version-timestamp.o` carries the build timestamp and
    build number in `UTS_VERSION`, and `init/version.o` carries the compiler
    identification that `scripts/mkcompile_h` reads from `$CC --version`
    (`crugc 0.1.0` for the launcher, `xgcc (GCC) 16.2.0` for the reference
    driver) plus the same timestamp; `kernel/object-diff-allow.txt` records both
    reasons. The two `.config` files likewise differ only in
    `CONFIG_CC_VERSION_TEXT`, which the script filters before comparing them.
- Not claimed: no `class K0` or `stage K0` result, no ownership contracts in kernel
  code, and no codegen-parity performance claim (that is Phase 21). A gate claim
  needs archived command output per `docs/crug/EXIT-GATES.md` section 2.

## 9. Amendments

- **A-1 (2026-10-20, Phase 19).** The object directory is `build/kernel-crug`
  (the earlier text said `build/kernel-6.18.57`); the reference build is
  `build/kernel-ref`; `kernel/KERNEL-CORPUS.md` publishes the 438-object x86_64
  corpus and the 15-object aarch64 subset; stage K6 now names its initramfs and
  `rdinit=/init`; stage K7 pins the arm64 `defconfig` and the cross compiler
  described in section 5; section 8 records the measured Phase 19 results. The pin
  itself (tag `v6.18.57`, commit `2ff600d`, size, digest) did not change.
- **A-2 (2026-10-22, Phase 25).** Phase 24 made `crugc --version` report the
  launcher's own identity, and the kernel copies that string into
  `init/version.o`, so that object joined `init/version-timestamp.o` as an
  explained difference: the count is 436 identical and 2 explained, not 437 and
  1. Section 8 and `kernel/object-diff-allow.txt` record both reasons, and
  `scripts/kernel-object-diff.sh` now ignores `CONFIG_CC_VERSION_TEXT` when it
  compares the two `.config` files, because that line carries the same
  identification. No compiler or runtime object changed; the boot and the
  build measurements are unaffected.
