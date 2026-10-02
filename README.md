# ccxview

A fast viewer for CalculiX results and models: `.frd`, `.inp`, `.dat`, and cgx
`.fbd`. Plain C99, one executable, no installation.

![demo](docs/demo.gif)

## Try it

**In the browser, nothing to install:** https://basemrajjoub.github.io/ccxview/
(WebGL2; the showcase model is preloaded, Open... or drag and drop your own
files, exports come as downloads).

Downloads of the [latest release](https://github.com/BasemRajjoub/ccxview/releases/latest)
(older versions and their changes: [all releases](https://github.com/BasemRajjoub/ccxview/releases)):

- **Linux**: [ccxview-linux-x86_64.tar.gz](https://github.com/BasemRajjoub/ccxview/releases/latest/download/ccxview-linux-x86_64.tar.gz) (glibc 2.34 or newer: Ubuntu 22.04,
  Debian 12, Fedora 35, RHEL 9 and later). Unpack, then double-click `ccxview`
  or run it from a terminal. Keep the `lib/` folder next to it.
- **Windows**: [ccxview-windows-x86_64.zip](https://github.com/BasemRajjoub/ccxview/releases/latest/download/ccxview-windows-x86_64.zip) (Windows 10/11, no DLLs needed).
- **Browser**: [ccxview-web.zip](https://github.com/BasemRajjoub/ccxview/releases/latest/download/ccxview-web.zip), the same page as the link above in one HTML
  file that works opened from disk.

## What it does

- **Fast.** Five million elements load in about a second; the view stays
  smooth on large models. Fields are decoded only when shown.
- **Renders on the GPU, or without one.** OpenGL 4.1 when a driver exists,
  otherwise ccxview restarts itself on Mesa's software renderer. The browser
  build runs on WebGL2.
- **Most CalculiX elements.** Hex, tet, wedge, shells, plane and axisymmetric
  elements, beams, trusses, springs, dashpots, masses, gaps, rigid bodies,
  couplings, ties and contact are read and drawn.
- **Fields on nodes, edges, faces, and Gauss points.** Any result component as
  colour on faces, edges or nodes; per-element (flat) colouring; vectors as
  arrows. Gauss points are drawn as balls at the integration points, coloured
  by the field, or by the exact values from a `.dat` file.
- **Decks too.** Open a `.inp` on its own: mesh, sets, surfaces, materials,
  supports and loads. With the `.frd` beside it, both are shown.
- **cgx geometry.** A `.fbd` shows its points, lines, surfaces and sets. Scripts
  are evaluated by cgx when it is installed; the mesh ELTY asks for is made even
  without cgx for lines, 4-sided surfaces and 6-sided bodies (mapped HE8/HE20,
  QU4/QU8, TR3/TR6, BE2/BE3).
- **Animation.** Mode shapes, steady-state phases, deformation cycles, step
  playback. Undeformed ghost, min/max markers, probe, find by id, path plots,
  compare two runs (A minus B), clip plane with the cut filled, crop box,
  mirror symmetry, cyclic symmetry, replicate (rows of copies of a periodic
  model), convergence plot from `.sta`/`.cvg`.
- **Post-processing.** Principal stresses with direction arrows, cylindrical
  coordinate systems, unit systems, history of a node or element over all
  steps, ASME VIII-2 stress linearization along a line through the wall
  (membrane, membrane + bending, peak and total at both ends and their
  largest value on the line; bending from the components normal to the line
  as 5-A.4.1.2 asks, or from all six).
- **Calculated fields.** Type a formula over the results and show it like any
  field, with its history and exports: `S1 - S3` (Tresca), `MISES / 235`,
  `sqrt(D1^2 + D2^2)`, `if(MISES > 200, 1, 0)`. Components, magnitudes, von
  Mises and principal values by name, node coordinates `X Y Z`, the step
  `TIME`, and the usual maths functions (TinyExpr).
- **Export.** PNG of the view, MP4 video or PNG sequence of a deformation
  cycle or of every step, CSV, VTK for ParaView, and a view file to reproduce
  a picture later. All available from the command line for scripting.
- **Robust.** Damaged records are skipped and listed, never a crash. Settings
  (in sections in `ccxview.ini`, beside the executable), open panel sections,
  window size and recent files are remembered. Should the viewer itself crash,
  it writes `ccxview-crash.txt` (beside it, else in the temp folder) with the
  version, the stack and the last log lines; attach it to an issue.
  `scripts/symbolize.sh` turns the stack into functions and lines, with the
  debug symbols of each release (`ccxview-symbols-x86_64.zip`).

## Use

```sh
ccxview model.frd
ccxview model.frd --shot out.png            # render, save, quit
ccxview model.frd --export mp4              # animation video
ccxview model.frd --field DISP --vectors    # displacement arrows
ccxview model.frd --gp                      # Gauss points
ccxview model.frd --field DISP --history 17 # node 17 over all steps
ccxview model.frd --calc "S1 - S3"          # a calculated field (Tresca)
ccxview model.frd --linearize 38,54         # ASME stress linearization, node 38 to 54
ccxview run2.frd --compare run1.frd         # difference of two runs
ccxview model.frd --software                # CPU rendering
ccxview --check model.frd                   # headless parse for CI
ccxview model.frd --ui-test DIR             # click through the interface, check it answers; failure pictures in DIR
```

Or Open... (Ctrl+O), drop a file on the window, or type a path in the box.

| Input | Action |
|---|---|
| left drag / right or middle drag / wheel | orbit / pan / zoom (about the point under the cursor; View panel: up axis Y or Z, turntable or free rotation, cursor pivot on/off, rotation centre mark) |
| X / Y / Z held + drag | orbit about that world axis only |
| Ctrl+drag / Ctrl+right drag | box zoom / zoom by dragging up and down |
| Alt+drag, Alt+← → | roll about the line of sight |
| middle click, C | centre the view on the point under the cursor (new rotation centre) |
| N | look normal to the face under the cursor |
| Ctrl+← → ↑ ↓ | turn the view 15° (with Shift 90°) |
| Ctrl+Z / Ctrl+Y | view back / forward |
| click / double-click | probe / zoom to element |
| F, R, 1..6 | fit, reset, look from ±X ±Y ±Z |
| space, ← → | play, step |
| G | free flight (WASD, Esc to leave) |
| H | view only, for screenshots |
| right-click legend | legend settings |
| Ctrl+O / Ctrl+E / Ctrl+F | open / export PNG / find |

## Build

```sh
make                 # Linux/macOS -> build/ccxview
make PORTABLE=1      # Linux build for glibc >= 2.34
make win             # Windows exe with zig cc or mingw-w64 -> build/win/ccxview.exe
make wasm            # browser build with Emscripten -> build/web/ccxview.html
build.bat            # Windows with MSVC (no console window; "build.bat console" for one)
make test            # unit tests
make uitest          # the interface clicked through by a script (src/ui_test.c); needs a display or Xvfb
scripts/pack-binaries.sh   # release archives in dist/
```

Add `-j8` to compile in parallel; every source is its own object, so a rebuild
compiles only what changed. `ccxview --version` prints the version from [VERSION](VERSION).

**Releasing.** Change the number in `VERSION` (e.g. `0.2.0`), commit and push to master.
GitHub Actions ([.github/workflows/release.yml](.github/workflows/release.yml)) then builds
Linux, Windows (mingw-w64) and the web in one job, runs the tests, creates the release
`v0.2.0` with the three archives and generated notes, and updates the browser version on
GitHub Pages. Other pushes build nothing. To build without releasing, run the workflow by
hand from the Actions tab (it keeps the archives as artifacts for a week).

Dependencies: a C compiler and, on Linux, X11 and OpenGL development headers.
Everything else is vendored (sokol, Nuklear, stb_image_write, stb_sprintf, TinyExpr, miniz,
minih264e, minimp4, tinyfiledialogs), and the UI fonts are compiled in (Inter, Noto Sans Math, Lucide icons;
regenerate with `scripts/embed-fonts.py`).

`samples/showcase/` is the plate with a hole preloaded in the browser build,
`samples/elements/` one small solved deck with every element type and feature,
`samples/vessel/` a pressure vessel for the stress linearization. Design notes
in [docs/spec.md](docs/spec.md); the `.inp` keywords the viewer reads in
[docs/keywords.md](docs/keywords.md).

License: GPL-2.0-or-later, the same as CalculiX. Vendored libraries keep
their own licences (zlib, MIT, public domain), see [vendor/README.md](vendor/README.md);
the embedded fonts are under the SIL Open Font License 1.1 (Inter, Noto Sans Math)
and ISC (Lucide), texts in [vendor/fonts/](vendor/fonts/).
