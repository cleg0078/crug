# CRUG container image: the installed compiler plus the Crug runtime.
#
# Multi-stage, built with the legacy builder (see build-image.sh):
#   builder  builds the pinned GCC baseline, libcrug and the install tree;
#   runtime  ships only the install tree (cc1, headers, libcrug.a).
#
# Build:  ./packaging/build-image.sh
# Run:    docker run --rm crug/pkg:0.1.0 crugc --version
#
# The builder stage deliberately does not pass --disable-bootstrap: the root
# Makefile's baseline target builds all-gcc from the in-repository fork tree.

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# The root Makefile's check-host list: make g++ gcc tar xz curl sha512sum
# python3 flex bison, plus the GMP/MPFR/MPC development libraries and zstd
# that the compiler itself links against.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential flex bison libgmp-dev libmpfr-dev libmpc-dev \
      zstd ca-certificates curl xz-utils python3 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Pinned GCC 16.2 baseline (download, verify, extract, prerequisites,
# configure out of tree, all-gcc), then the Crug runtime objects.
RUN make baseline JOBS=$(nproc)
RUN make -C libcrug PAL=linux

# Build the configured target libraries (libgcc, libstdc++, libatomic,
# libgomp, libquadmath, libsanitizer, ...) in parallel.  scripts/package.sh
# runs "make install", which installs every configured target library; without
# this step make would build them one after another inside the install.
RUN make -C build/obj -j$(nproc) all-target
# The install step also descends into the configured host directories
# (c++tools, libiberty, lto-plugin, libcc1, ...), so build those in parallel
# too; "make install" alone would build each of them serialised.
RUN make -C build/obj -j$(nproc) all-host

# scripts/package.sh installs with DESTDIR into /stage/opt/crug: bin/crugc,
# include/crug, lib/libcrug.a and share/doc/crug.
RUN scripts/package.sh --version "$(cat VERSION)" --stage /stage

# The smoke programs are small and self-contained; ship their sources with the
# documentation so the image can run its own validation without a bind mount.
RUN install -d /stage/opt/crug/share/doc/crug/smoke \
 && install -m 0644 packaging/smoke/hello.c packaging/smoke/hello-own.c \
      packaging/smoke/runtime-sample.c \
      /stage/opt/crug/share/doc/crug/smoke/

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# cc1 needs libzstd; as/ld come from binutils; libc6-dev supplies the CRT files
# and the C library headers a hosted compile needs.
RUN apt-get update \
 && apt-get install -y --no-install-recommends binutils libc6-dev libzstd1 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=builder /stage/opt/crug /opt/crug
ENV PATH=/opt/crug/bin:$PATH

CMD ["crugc", "--version"]
