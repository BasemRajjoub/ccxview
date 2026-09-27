#!/usr/bin/env bash
# bundle.sh -- make build/ self-contained on Linux.
#
#   build/ccxview        launcher (sh): runs bin/ccxview against the SYSTEM libraries
#                        first; only when the loader cannot find one (exit 127) it
#                        runs again with lib/ on LD_LIBRARY_PATH.
#   build/bin/ccxview    the binary, no RUNPATH
#   build/lib/           the X11 family and the GLVND libGL dispatcher, copied from
#                        the build host, for machines that lack them
#   build/lib/mesa/      (only with --mesa) Mesa's llvmpipe software renderer and
#                        its closure, used by the no-GPU fallback when the host has
#                        no OpenGL at all. Large (libLLVM), so opt-in.
#
# libc / libm / libdl / libpthread / the loader are never bundled: they must be
# the host's. Build inside scripts/build-portable.sh for a low glibc floor.
set -euo pipefail

BIN="${1:-build/bin/ccxview}"
MESA=0
[ "${2:-}" = "--mesa" ] && MESA=1
OUT="$(cd "$(dirname "$BIN")/.." && pwd)"
LIBDIR="$OUT/lib"
ENV_LIB=""
for c in "${CONDA_PREFIX:-}" "$(dirname "$(dirname "$(command -v cc)")")"; do
    [ -n "$c" ] && [ -e "$c/lib/libX11.so.6" ] && { ENV_LIB="$c/lib"; break; }
done
mkdir -p "$LIBDIR"

skip_lib() {
    case "$1" in
        libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|ld-linux*|linux-vdso*|libresolv.so.*) return 0 ;;
    esac
    return 1
}

# copy_closure TARGET DESTDIR: every shared library TARGET resolves, recursively
copy_closure() {
    local target="$1" dest="$2" lib base src
    for lib in $(ldd "$target" 2>/dev/null | awk '{ for (i=1;i<=NF;i++) if ($i ~ /^\//) { print $i; break } }'); do
        base="$(basename "$lib")"
        skip_lib "$base" && continue
        [ -e "$dest/$base" ] || [ -e "$LIBDIR/$base" ] && continue
        src="$lib"
        [ -n "$ENV_LIB" ] && [ -e "$ENV_LIB/$base" ] && src="$ENV_LIB/$base"
        cp -L "$src" "$dest/$base"
        chmod u+w "$dest/$base"                # store copies may be read-only; patchelf needs to write
        copy_closure "$src" "$dest"
    done
}
copy_closure "$BIN" "$LIBDIR"

if command -v patchelf >/dev/null; then
    # the binary carries no RUNPATH: the system's libraries win, lib/ is the fallback
    patchelf --remove-rpath "$BIN" 2>/dev/null || true
    for f in "$LIBDIR"/*.so*; do patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true; done
    # GLVND's libGLX dlopen()s the real driver and searches the CALLER's RUNPATH.
    # NixOS keeps drivers only in /run/opengl-driver/lib; elsewhere the entry is
    # simply skipped.
    for f in "$LIBDIR"/libGLX.so.* "$LIBDIR"/libGL.so.*; do
        [ -e "$f" ] && patchelf --set-rpath '$ORIGIN:/run/opengl-driver/lib' "$f"
    done
else
    echo "note: patchelf not found; RUNPATH left as linked" >&2
fi

# ---- Mesa llvmpipe, opt-in --------------------------------------------------------
if [ "$MESA" = 1 ]; then
    MDIR="$LIBDIR/mesa"
    mkdir -p "$MDIR"
    glx=""
    for d in /run/opengl-driver/lib /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib "$ENV_LIB"; do
        [ -n "$d" ] && [ -e "$d/libGLX_mesa.so.0" ] && { glx="$d/libGLX_mesa.so.0"; break; }
    done
    if [ -z "$glx" ]; then
        echo "note: libGLX_mesa.so.0 not found on this host; no software renderer bundled" >&2
    else
        [ -e "$MDIR/libGLX_mesa.so.0" ] || cp -L "$glx" "$MDIR/libGLX_mesa.so.0"
        chmod u+w "$MDIR/libGLX_mesa.so.0"
        copy_closure "$glx" "$MDIR"
        mdir="$(dirname "$(readlink -f "$glx")")"
        # the software rasteriser itself: one big libgallium (Mesa >= 24) or dri/swrast_dri.so (older)
        for g in "$mdir"/libgallium*.so* "$mdir"/dri/swrast_dri.so "$mdir"/dri/libgallium*; do
            [ -e "$g" ] || continue
            [ -e "$MDIR/$(basename "$g")" ] || cp -L "$g" "$MDIR/$(basename "$g")"
            chmod u+w "$MDIR/$(basename "$g")"
            copy_closure "$g" "$MDIR"
        done
        for f in "$MDIR"/*.so*; do patchelf --set-rpath '$ORIGIN:$ORIGIN/..' "$f" 2>/dev/null || true; done
        echo "Mesa llvmpipe bundled into $MDIR ($(du -sh "$MDIR" | cut -f1))"
    fi
fi

# ---- launcher ------------------------------------------------------------------------
cat > "$OUT/ccxview" <<'EOF'
#!/bin/sh
# ccxview launcher: the system's libraries first; lib/ only when one is missing.
d="$(cd "$(dirname "$(readlink -f "$0" 2>/dev/null || echo "$0")")" && pwd)"
err="$(mktemp 2>/dev/null || echo /tmp/ccxview.$$)"
"$d/bin/ccxview" "$@" 2>"$err"
rc=$?
if [ "$rc" -eq 127 ] && grep -q "error while loading shared libraries" "$err"; then
    rm -f "$err"
    LD_LIBRARY_PATH="$d/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" exec "$d/bin/ccxview" "$@"
fi
cat "$err" >&2
rm -f "$err"
exit "$rc"
EOF
chmod +x "$OUT/ccxview"

echo "bundled into $LIBDIR:"
ls -1 "$LIBDIR" | grep -v '^mesa$' | sed 's/^/    /'
floor="$( { objdump -T "$BIN"; objdump -T "$LIBDIR"/*.so* 2>/dev/null; } |
          grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1 )"
echo "requires glibc >= $floor. Copy the whole $(basename "$OUT") directory; run $(basename "$OUT")/ccxview."
