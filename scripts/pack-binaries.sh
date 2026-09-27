#!/usr/bin/env bash
# pack-binaries.sh -- put ready-to-run builds into binaries/:
#   binaries/linux/ccxview   the binary, RUNPATH $ORIGIN/lib: double-click runs it
#   binaries/linux/lib/      X11 + GLVND libraries for systems that lack them
#   binaries/windows/ccxview.exe
#   binaries/web/ccxview.html   the browser build, one file, opens from disk or any web host
# Expects build/bin/ccxview (make PORTABLE=1), build/win/ccxview.exe (make win)
# and docs/index.html (make wasm).
set -euo pipefail
cd "$(dirname "$0")/.."
rm -rf binaries/linux binaries/windows binaries/web
mkdir -p binaries/linux/lib binaries/windows binaries/web
cp build/bin/ccxview binaries/linux/ccxview
cp build/lib/*.so* binaries/linux/lib/
chmod 755 binaries/linux/ccxview binaries/linux/lib/*
strip binaries/linux/ccxview binaries/linux/lib/*.so* 2>/dev/null || true   # no debug info, no build paths
if command -v patchelf >/dev/null; then
    patchelf --set-rpath '$ORIGIN/lib' binaries/linux/ccxview
    patchelf --set-interpreter /lib64/ld-linux-x86-64.so.2 binaries/linux/ccxview   # not the build host's loader (NixOS)
    for f in binaries/linux/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true; done
    for f in binaries/linux/lib/libGLX.so.* binaries/linux/lib/libGL.so.*; do
        [ -e "$f" ] && patchelf --set-rpath '$ORIGIN:/run/opengl-driver/lib' "$f"
    done
fi
if [ -e build/win/ccxview.exe ]; then
    cp build/win/ccxview.exe binaries/windows/ccxview.exe
    "${MINGW_STRIP:-x86_64-w64-mingw32-strip}" binaries/windows/ccxview.exe 2>/dev/null || true
fi
[ -e docs/index.html ] && cp docs/index.html binaries/web/ccxview.html
floor="$(objdump -T binaries/linux/ccxview | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)"
echo "binaries/linux/ccxview needs glibc >= $floor; binaries/windows/ccxview.exe $(du -h binaries/windows/ccxview.exe 2>/dev/null | cut -f1)"
