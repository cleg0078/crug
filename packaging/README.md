# CRUG packages: the Debian package and the container image (Phase 24)

Two installable artefacts are built from one install tree, so they contain the
same compiler: `dist/crug_<version>-<revision>_amd64.deb` and the container
image `crug/pkg:<version>`. The version is read from the repository `VERSION`
file, currently `0.2.4`.

The install tree itself (`/opt/crug`, the tarball root) contains:

    /opt/crug/bin/crugc          the compiler launcher (seven-line --version)
    /opt/crug/include/crug/      task.h, chan.h, crug_pal.h
    /opt/crug/lib/libcrug.a      the runtime (task, channels, stack, PAL)
    /opt/crug/share/doc/crug/    LICENSE, NOTICE, CHANGELOG.md, VERSION and the
                                 smoke sources

## The Debian package

    $ make package-deb          # or: scripts/build-deb.sh

`make package-deb` runs `scripts/package.sh` first unless the stage is already
there (`--reuse-stage`), so the package is never built from a stale tree.
`make package` builds the tarball, the package, the SBOM and the checksums
together.

| Path in the package | Contents |
|---------------------|----------|
| `/opt/crug/` | The complete install tree listed above. |
| `/usr/bin/crugc` | Absolute link to `/opt/crug/bin/crugc`. |
| `/usr/include/crug/` | Link to `/opt/crug/include/crug`. |
| `/usr/lib/x86_64-linux-gnu/libcrug.a` | Link to `/opt/crug/lib/libcrug.a`. |
| `/usr/share/doc/crug/` | `copyright` (DEP-5) and `changelog.Debian.gz`. |

The control file is generated from `packaging/debian/control.in`. The
`Depends` line is measured while the package is built: `libc6 (>= X)` where X
is the highest `GLIBC_*` symbol version in any ELF file of the payload, plus
one package per `NEEDED` soname, plus `binutils` and `libc6-dev` because
compiling needs the assembler, the linker and the system headers. There are no
maintainer scripts, no `postinst` and no `prerm`; the package only places
files and links.

    $ sudo apt-get install -y ./dist/crug_0.2.4-1_amd64.deb
    $ crugc --version
    $ sudo apt-get remove crug

`make check-DEB` verifies the package without installing it: control fields,
the payload allow-list (nothing may land in `/usr/bin/gcc` or any
`/usr/bin/x86_64-*`), owners and modes (root/root, no setuid, no group or
other write), the generated `md5sums`, the declared `Depends` against the
sonames and the GLIBC floor re-measured from the package, the packaged
launcher's seven version lines, and the three smoke programs compiled and run
from an extraction. `make check-PACKAGING` also installs the package with
`apt` in a clean `ubuntu:24.04`, runs the three programs through
`/usr/bin/crugc`, and removes it again.

It is a multi-stage build: `ubuntu:24.04` as builder installs the build
dependencies, copies the repository, runs `make baseline JOBS=$(nproc)`,
builds the configured target libraries and host directories in parallel
(`make -C build/obj -j$(nproc) all-target` and `... all-host`, because
`make install` installs both without building them first), installs the Crug
runtime (`make -C libcrug PAL=linux`) and stages the install tree with
`scripts/package.sh --version $(cat VERSION) --stage /stage`; the runtime
stage is a clean `ubuntu:24.04` with
binutils, libc6-dev and libzstd1 (cc1 links zstd) and only `/opt/crug` from
the builder, with `/opt/crug/bin` on `PATH`.

## Build

    ./packaging/build-image.sh

The script forces the legacy builder (`DOCKER_BUILDKIT=0`, buildx is not
usable in this environment) and `--network host`, and stages a pruned copy of
the repository under `build/packaging/context` (without `.git`, `build/`,
`sources/`, `packaging/Dockerfile` and `packaging/build-image.sh`) because
the legacy builder uploads the whole context; the Dockerfile itself is passed
with `-f` from the repository.  The
first build compiles the pinned GCC baseline and takes a while; the script
prints the image tag when it is done.  Override the tag with
`CRUG_IMAGE_TAG=...`.

## Version

    docker run --rm crug/pkg:0.2.4 crugc --version

Expected output (one line per version, RFC-0001 section 8.2):

    crugc 0.2.4
    compiler: GCC 16.2.0
    language version: 0.1.0
    Crug ABI version: 1
    runtime ABI version: 1
    FFI metadata version: 1.0
    PAL version: 1.2

## The three string smoke programs

The sources are in the image at `/opt/crug/share/doc/crug/smoke/`, so every
command is self-contained (no bind mount).  Each program compiles inside the
container and its exit status is checked.

### 1. Plain C

    docker run --rm crug/pkg:0.2.4 sh -c 'crugc -O2 /opt/crug/share/doc/crug/smoke/hello.c -o /tmp/hello && /tmp/hello'

Expected output line:

    hello from crug

### 2. Ownership

    docker run --rm crug/pkg:0.2.4 sh -c 'crugc -O2 -fcrug-ownership /opt/crug/share/doc/crug/smoke/hello-own.c -o /tmp/hello-own && /tmp/hello-own'

Expected output line:

    own-ok value=42

The heap buffer is adopted in `main`, moved into `take()`, adopted there and
released by the destructor the compiler attaches to the end of `take()`'s
scope; the source has no `free()` call.

### 3. Runtime (go + channels)

    docker run --rm crug/pkg:0.2.4 sh -c 'crugc -O2 -fcrug-goroutines -fcrug-channels -I/opt/crug/include/crug /opt/crug/share/doc/crug/smoke/runtime-sample.c -L/opt/crug/lib -lcrug -pthread -o /tmp/runtime-sample && /tmp/runtime-sample'

Expected output line:

    runtime-ok sum=7

A `scope` starts three tasks that send 1, 2 and 4 on a buffered channel; the
block joins them, `main` receives the three values, and the runtime shutdown
joins the worker threads.

## Validation sequence

Run the four commands above in order, from the repository root; the expected
lines are `hello from crug`, `own-ok value=42` and `runtime-ok sum=7`, and
the version command prints the seven lines listed above.  All of them exit 0.
