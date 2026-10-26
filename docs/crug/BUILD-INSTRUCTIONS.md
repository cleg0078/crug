# Building and installing Crug

Status: **Phase 24 implemented 2026-10-21; the Debian package added 2026-10-22;
0.2.0 defect batch 2026-10-26.** This page is the build and release guide for
Crug 0.2.0 on Linux x86-64: the source build, the release artefacts made by
`scripts/package.sh`, the container image, the Debian package, the install
tree, the verification commands and the supported hosts. The byte counts and
artifact names quoted below are the recorded 0.2.0 transcript; read the current
version from `VERSION` and regenerate the artifacts for a new release. The
image mechanics are in the [packaging README](../../packaging/README.md); the
other hosts are in
[PORTABILITY.md](PORTABILITY.md). The version is read from
[the VERSION file](../../VERSION) and reported by `crugc --version`.

## 1. Prerequisites

A Linux x86-64 host with `make`, `gcc`, `g++` (C++14), `tar`, `xz`,
`curl`, `sha512sum`, `python3`, `flex` and `bison`. `make check-host`
checks the list and the disk space:

    $ make check-host
      make       /usr/bin/make
      g++        /usr/bin/g++
      gcc        /usr/bin/gcc
      tar        /usr/bin/tar
      xz         /usr/bin/xz
      curl       /usr/bin/curl
      sha512sum  /usr/bin/sha512sum
      python3    /usr/bin/python3
      flex       /usr/bin/flex
      bison      /usr/bin/bison
      C++14      ok (13.3.0)

Exit status 0 means every tool is present and the host compiler accepts
`-std=c++14`; a missing tool prints `MISSING` and exits 1. A full build
wants about 10 GiB (`build/obj` is 5.2 GiB on the reference host); the
pruned release stage adds 433 MiB and `dist/` 143 MiB, so 16 GiB free is
comfortable.
DejaGnu is only needed for the compiler test suite:
`scripts/bootstrap-dejagnu.sh` installs it under `tools/` without root.

## 2. Build from source

    $ make baseline JOBS=$(nproc)
    $ make -C libcrug PAL=linux

`make baseline` runs the pinned-baseline stages in order — `download`,
`verify` (against `docs/crug/gcc-baseline.sha512`), `extract`, `deps`,
`configure` and `build` — configuring out of tree in `build/obj` with
`--prefix=/opt/crug`, the prefix the release tree uses. It ends with:

    baseline ready in build/obj
    xgcc (GCC) 16.2.0

`make xgcc-version` prints that second line on its own. The in-tree launcher
`gcc/crugc` runs `build/obj/gcc/xgcc`, so the compiler is usable before it is
installed:

    $ sh gcc/crugc --version
    crugc 0.2.0
    compiler: GCC 16.2.0
    language version: 0.1.0
    Crug ABI version: 1
    runtime ABI version: 1
    FFI metadata version: 1.0
    PAL version: 1.2

`make -C libcrug PAL=linux` builds the runtime objects in
`libcrug/build/linux/` (`task.o`, `pal_linux.o`, `switch.o`, `stack.o`,
`chan.o`). The release archive is exactly those five objects.

`make baseline` stops at `all-gcc`, while the full install also walks the host
directories (`c++tools`, `lto-plugin`, `fixincludes`) and the target libraries.
`scripts/package.sh` builds those with
`make -C build/obj -j$(nproc) all-target all-host` when it is not given
`--skip-build` (a no-op on a tree that `make` or `make all` already built), so
section 3 works directly on a fresh baseline. To prepare the tree by hand:

    $ make -C build/obj -j$(nproc) all-target all-host

## 3. Build the release artefacts

    $ scripts/package.sh
    $ sh scripts/check-release.sh

`scripts/package.sh` reads the version from [VERSION](../../VERSION)
(0.2.0), installs the compiler into a private stage, adds
`bin/crugc`, the three Crug headers and `lib/libcrug.a`, prunes the C++
front end (`bin/c++`, `bin/g++`, `cc1plus`, `include/c++`), strips the
compiler with `strip --strip-all` (`cc1` falls from 480432544 to 54540624
bytes), and then writes the artefacts into `dist/`:

| Artefact | Contents |
|----------|----------|
| `dist/crug-0.2.0-linux-x86_64.tar.gz` | The install tree, rooted at `crug-0.2.0/`, which is the `/opt/crug` tree: about 143 MB in 971 files. |
| `dist/crug_0.2.0-1_amd64.deb` | The Debian package of the same tree plus the `/usr/bin/crugc`, `/usr/include/crug` and `/usr/lib/x86_64-linux-gnu/libcrug.a` links and the Debian documentation: about 94 MB, 433 MiB installed. |
| `dist/crug-0.2.0.spdx.json` | SPDX 2.3 SBOM for the tarball, with the `crug`, `gcc-16.2.0` and `libcrug` packages and one entry per file. |
| `dist/LICENSE`, `dist/NOTICE` | The licence texts shipped with the release. |
| `dist/SHA256SUMS` | The SHA-256 of the five artefacts above, in `sha256sum` format. |

The run reports the runtime build, the install, the prune-and-strip step and
the writing steps as it goes, and ends with these lines:

    package: pruned and stripped (cc1 480432544 -> 54540624 bytes)
    package: crug-0.2.0-linux-x86_64.tar.gz 143163019 bytes, 971 files
    package: dist/crug-0.2.0.spdx.json
    deb: dist/crug_0.2.0-1_amd64.deb 93733800 bytes, 976 files, installed-size 443428 KiB
    package: SHA256SUMS: 5 artefact(s)

The byte counts and the installed size move by a few bytes between builds (the
package is not byte-reproducible), so read them from the run; the transcript
above is the 2026-10-22 build. The tar is built deterministically: sorted names, `--mtime=@0`, owner and group
0, numeric owner, and `gzip -n`; the GNU ar archive it contains is
deterministic as well. Flags override the defaults:

    $ scripts/package.sh --version 0.2.0 --out dist --stage build/package/crug-0.2.0
    $ scripts/package.sh --skip-build     # fail rather than build missing inputs
    $ CRUG_DEB=skip scripts/package.sh --skip-build   # tarball only, no Debian package

The Debian package is built by `scripts/build-deb.sh`, which `package.sh` runs
after the stage exists (so the package is never stale). It measures the
payload before writing the control file:

    deb: measuring the payload dependencies
    deb: depends: libc6 (>= 2.38), libgcc-s1, libstdc++6, libzstd1, binutils, libc6-dev

`libc6 (>= 2.38)` is the highest `GLIBC_*` symbol version found in any ELF
file of the payload; the other names come from the `NEEDED` sonames the tree
links against, and `binutils` and `libc6-dev` are added because `crugc`
runs `as`/`ld` and compiles against the system headers. `scripts/check-deb.sh`
re-measures both from the built package and fails when the declared line
disagrees with it.

`scripts/check-release.sh` then checks version consistency across
[VERSION](../../VERSION), [CHANGELOG.md](../../CHANGELOG.md) and
`gcc/crugc --version` (a release-candidate entry named `[<version>-rcN]`
counts for that version), that `SHA256SUMS` lists exactly the tarball, the
Debian package, the SBOM and the two licence texts, the checksums, the SBOM
fields and the tarball entries, and prints:

    check-release: 24 check(s), 0 problem(s)

## 4. Build and use the container image

    $ ./packaging/build-image.sh
    $ docker run --rm crug/pkg:0.2.0 crugc --version

The image is a multi-stage build: `ubuntu:24.04` builds the pinned baseline
and runs `scripts/package.sh --version "$(cat VERSION)" --stage /stage`; the
runtime stage copies only `/stage/opt/crug` to `/opt/crug` and puts
`/opt/crug/bin` on `PATH`. The version command prints the seven lines listed
in section 2. The three smoke programs ship inside the image, so each command
is self-contained:

    $ docker run --rm crug/pkg:0.2.0 sh -c 'crugc -O2 /opt/crug/share/doc/crug/smoke/hello.c -o /tmp/hello && /tmp/hello'
    hello from crug
    $ docker run --rm crug/pkg:0.2.0 sh -c 'crugc -O2 -fcrug-ownership /opt/crug/share/doc/crug/smoke/hello-own.c -o /tmp/hello-own && /tmp/hello-own'
    own-ok value=42
    $ docker run --rm crug/pkg:0.2.0 sh -c 'crugc -O2 -fcrug-goroutines -fcrug-channels -I/opt/crug/include/crug /opt/crug/share/doc/crug/smoke/runtime-sample.c -L/opt/crug/lib -lcrug -pthread -o /tmp/runtime-sample && /tmp/runtime-sample'
    runtime-ok sum=7

## 5. Install tree layout

The tarball root `crug-0.2.0/` is the install tree, so installing into the
configured prefix is:

    $ sudo mkdir -p /opt/crug
    $ sudo tar -xzf dist/crug-0.2.0-linux-x86_64.tar.gz -C /opt/crug --strip-components=1

| Path under `/opt/crug` | Contents |
|-------------------------|----------|
| `bin/crugc` | The Crug launcher; its `--version` is the seven-line version report. |
| `bin/gcc`, `bin/x86_64-pc-linux-gnu-gcc-16.2.0` | The GCC 16.2 C driver and its aliases. |
| `include/crug/task.h`, `include/crug/chan.h`, `include/crug/crug_pal.h` | The Crug runtime headers. |
| `lib/libcrug.a` | The Crug runtime: tasks, channels, stacks and the Linux PAL. |
| `libexec/gcc/`, `lib/gcc/` | The C compiler proper (`cc1`, `lto1`, `collect2`, `lto-wrapper`) and its support files. |
| `share/doc/crug/` | `LICENSE`, `NOTICE`, `CHANGELOG.md`, `VERSION` and the smoke sources in the image. |

The driver is configured with `--prefix=/opt/crug`, the prefix the release
artefacts are built for; keep the tree there and put `/opt/crug/bin` on
`PATH`, then `crugc --version` works without further setup.

### The Debian package

The package installs the same tree under `/opt/crug`, so nothing in it is
compiled for a different layout, and adds four entries outside that tree:

    $ sudo apt-get install -y ./dist/crug_0.2.0-1_amd64.deb

| Path | Contents |
|------|----------|
| `/opt/crug/` | The complete install tree of section 5 (compiler, `cc1`, runtime headers and `libcrug.a`). |
| `/usr/bin/crugc` | Absolute link to `/opt/crug/bin/crugc`; the launcher follows its own symlinks, so this runs the packaged compiler. |
| `/usr/include/crug/` | Link to `/opt/crug/include/crug`, so `#include <crug/task.h>` works without `-I`. |
| `/usr/lib/x86_64-linux-gnu/libcrug.a` | Link to `/opt/crug/lib/libcrug.a`, so `-lcrug` works without `-L`. |
| `/usr/share/doc/crug/` | `copyright`, `changelog.Debian.gz` and the licence texts. |

The package declares `libc6 (>= 2.38)`, `libgcc-s1`, `libstdc++6`, `libzstd1`,
`binutils` and `libc6-dev`: the first is measured from the payload, the next
three from its `NEEDED` entries, and the last two are what compiling needs
(`as`, `ld` and the C headers). There are no maintainer scripts, so
`apt-get remove crug` deletes the links and the tree and nothing else.

    $ crugc --version
    $ crugc hello.c -o hello && ./hello
    $ crugc -fcrug-goroutines -fcrug-channels -I/opt/crug/include/crug runtime-sample.c -L/opt/crug/lib -lcrug -pthread -o runtime && ./runtime

`make check-DEB` verifies the package itself: control fields, the payload
allow-list, owners and modes, the generated `md5sums`, the measured
dependency floor, the packaged launcher's seven version lines and the three
smoke programs compiled and run from an extraction. `make check-PACKAGING`
additionally installs the package with `apt` in a clean `ubuntu:24.04`,
runs the same three programs through `/usr/bin/crugc`, and removes the
package again.

## 6. Verify

From the repository root, after `scripts/package.sh`:

    $ cd dist && sha256sum -c SHA256SUMS
    crug-0.2.0-linux-x86_64.tar.gz: OK
    crug_0.2.0-1_amd64.deb: OK
    crug-0.2.0.spdx.json: OK
    LICENSE: OK
    NOTICE: OK

    $ tar -tzf dist/crug-0.2.0-linux-x86_64.tar.gz | wc -l
    1074

The listing has 1074 entries (971 files) and starts with
`crug-0.2.0/bin/crugc`; `crug-0.2.0/lib/libcrug.a`,
`crug-0.2.0/include/crug/task.h` and the four
`crug-0.2.0/share/doc/crug/` files are present. A quick smoke test of the
extracted launcher:

    $ mkdir -p /tmp/crug-tree
    $ tar -xzf dist/crug-0.2.0-linux-x86_64.tar.gz -C /tmp/crug-tree --strip-components=1
    $ /tmp/crug-tree/bin/crugc --version
    crugc 0.2.0
    ...

`make check-PACKAGING` runs the full Phase 24 acceptance flow: it rebuilds
the artefacts, runs `check-release.sh`, checks the SBOM and the tarball
listing independently, runs the image version command and the three smoke
programs (it builds the image when it is absent), and installs the Debian
package with `apt` in a clean `ubuntu:24.04` before removing it again. The
last line is `packaging: OK`.

## 7. Supported hosts

The release tarball and the container image target Linux x86-64. The compiler
and the runtime also build and run on the other hosts in
[PORTABILITY.md](PORTABILITY.md):

| Host | Crug 0.2.0 |
|------|------------|
| Linux x86-64 | The packaged target: this page, the tarball, the image and the full gate set. |
| Linux AArch64 | Built from source with the AArch64 PAL; `make check-MULTI_OS TARGET=aarch64-linux` covers the runtime on that host. |
| Windows x86-64 | Built from source with the Windows PAL; `make check-MULTI_OS` covers it on that host. |
| macOS ARM64 | Built from source with the macOS PAL; `make check-MULTI_OS` covers it. |
| macOS x86-64 | Built from source; the multi-OS gate record is ARM64. |

Each of those hosts starts from section 2 (`make baseline`, then the platform
runtime) and its own linker and system headers.
