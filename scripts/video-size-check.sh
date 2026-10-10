#!/bin/sh
# video-size-check.sh [binary] -- the video of `--no-panels --export mp4steps` must be
# the whole view (the window less the 2 px frame), not the view the panels left
# before the model was loaded. Needs a display (DISPLAY); the video goes to build/.
#   DISPLAY=:99 scripts/video-size-check.sh
bin=${1:-build/ccxview}
out=build/video-size-check
rm -rf "$out"; mkdir -p "$out"
cp samples/cantilever/cantilever.* "$out/"
"$bin" "$out/cantilever.frd" --no-sidecar --size 900x700 --no-panels --opt fps=8 --export mp4steps \
    --shot "$out/last.png" --frames 5 >/dev/null 2>&1
mp4=$(ls "$out"/*.mp4 2>/dev/null | head -1)
[ -n "$mp4" ] || { echo "video-size-check: no video written"; exit 1; }
# the size in the track header (tkhd): width and height, 16.16 fixed point, its last 8 bytes
python3 - "$mp4" <<'EOF'
import struct, sys
b = open(sys.argv[1], "rb").read()
i = b.find(b"tkhd")
size = struct.unpack(">I", b[i - 4:i])[0]
w, h = struct.unpack(">II", b[i - 4 + size - 8:i - 4 + size])
w, h = w >> 16, h >> 16
ok = (w, h) == (896, 696)
print(f"video-size-check: {w}x{h}, want 896x696: {'ok' if ok else 'FAILED'}")
sys.exit(0 if ok else 1)
EOF
