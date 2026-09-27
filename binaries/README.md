# Ready-to-run builds

- `linux/ccxview` — 64-bit Linux, needs glibc 2.34 or newer (Ubuntu 22.04,
  Debian 12, Fedora 35, RHEL 9 and later). Double-click it or run it from a
  terminal. The `lib/` folder beside it holds the X11 and OpenGL dispatch
  libraries for minimal systems; keep it next to the binary. The GPU driver
  comes from your system; without one, ccxview restarts itself on Mesa's
  software renderer.
- `windows/ccxview.exe` — 64-bit Windows 10/11, no installation, no DLLs.
  Without a GPU driver, put Mesa's `opengl32.dll` in a `mesa` folder beside it.
- `web/ccxview.html` — the browser build (WebGL2). One file: open it from
  disk or put it on any web server. Open... and drag and drop take files from
  your machine; exports arrive as downloads. Same as the live page at
  https://basemrajjoub.github.io/ccxview/.

All three are what `make PORTABLE=1 && make win && make wasm &&
scripts/pack-binaries.sh` produce.
