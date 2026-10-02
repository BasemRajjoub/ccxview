#!/usr/bin/env bash
# pack-binaries.sh -- the release archives, from whatever has been built:
#   dist/ccxview-linux-x86_64.tar.gz   ccxview (RUNPATH $ORIGIN/lib) + lib/ (X11 and
#                                      GLVND libraries for systems that lack them)
#   dist/ccxview-windows-x86_64.zip    ccxview.exe
#   dist/ccxview-web.zip               ccxview.html, one file: opens from disk or any web host
# Each holds README.txt and licenses/ (GPL for ccxview, the embedded fonts' and the
# vendored libraries' licences).
# Takes build/bin/ccxview + build/lib/ (make PORTABLE=1), build/win/ccxview.exe
# (make win), build/web/ccxview.html (make wasm). CI runs it for every release.
set -euo pipefail
cd "$(dirname "$0")/.."
rm -rf dist && mkdir -p dist
stage() {   # stage NAME: a fresh dist/NAME/ with the licences and the readme
    mkdir -p "dist/$1/licenses"
    cp LICENSE "dist/$1/licenses/LICENSE-ccxview.txt"
    cp vendor/fonts/*.txt vendor/licenses/*.txt "dist/$1/licenses/"
    cp scripts/release-readme.txt "dist/$1/README.txt"
}
if [ -e build/bin/ccxview ]; then
    d=dist/ccxview-linux-x86_64
    stage ccxview-linux-x86_64
    mkdir -p "$d/lib"
    cp build/bin/ccxview "$d/ccxview"
    cp build/lib/*.so* "$d/lib/"
    chmod 755 "$d/ccxview" "$d"/lib/*
    strip "$d/ccxview" "$d"/lib/*.so* 2>/dev/null || true   # no debug info, no build paths
    if command -v patchelf >/dev/null; then
        patchelf --set-rpath '$ORIGIN/lib' "$d/ccxview"
        patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 "$d/ccxview"   # not the build host's loader (NixOS)
        for f in "$d"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true; done
        for f in "$d"/lib/libGLX.so.* "$d"/lib/libGL.so.*; do
            [ -e "$f" ] && patchelf --set-rpath '$ORIGIN:/run/opengl-driver/lib' "$f"
        done
    fi
    tar -C dist -czf "$d.tar.gz" ccxview-linux-x86_64
    floor="$(objdump -T "$d/ccxview" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)"
    echo "linux: needs glibc >= $floor"
fi
if [ -e build/win/ccxview.exe ]; then
    stage ccxview-windows-x86_64
    cp build/win/ccxview.exe dist/ccxview-windows-x86_64/
    (cd dist && zip -qr ccxview-windows-x86_64.zip ccxview-windows-x86_64)
fi
if [ -e build/web/ccxview.html ]; then
    stage ccxview-web
    cp build/web/ccxview.html dist/ccxview-web/
    (cd dist && zip -qr ccxview-web.zip ccxview-web)
fi
ls -l dist/*.tar.gz dist/*.zip 2>/dev/null
