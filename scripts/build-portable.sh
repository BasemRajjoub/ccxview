#!/usr/bin/env sh
# build-portable.sh -- Linux release build inside a manylinux2014 container
# (CentOS 7, glibc 2.17), so the binary and the bundled X11/GL libraries run on
# any distribution since about 2014. Needs podman or docker. Output: build/
# (launcher, bin/, lib/), and lib/mesa with --mesa.
#
#   scripts/build-portable.sh          # binary + X11/GL closure
#   scripts/build-portable.sh --mesa   # + Mesa llvmpipe (software renderer, large)
set -e
cd "$(dirname "$0")/.."
RUN="$(command -v podman || command -v docker)" || { echo "podman or docker needed" >&2; exit 1; }
IMG=quay.io/pypa/manylinux2014_x86_64
mesa="${1:-}"
"$RUN" run --rm -v "$PWD:/src:Z" -w /src "$IMG" bash -ec '
    yum install -y -q libX11-devel libXi-devel libXcursor-devel mesa-libGL-devel patchelf \
        $( [ "$1" = --mesa ] && echo mesa-dri-drivers ) >/dev/null
    rm -rf build
    make CC=gcc build/bin/ccxview
    scripts/bundle.sh build/bin/ccxview '"$mesa"'
    make build/bench build/gen_frd >/dev/null
    chown -R "$(stat -c %u:%g /src/Makefile)" build
' -- "$mesa"
echo "portable build in build/:"
ls -1 build
