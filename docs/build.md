# Building ccxview

```sh
make                 # Linux/macOS -> build/ccxview
make PORTABLE=1      # Linux build for glibc >= 2.34
make win             # Windows exe with zig cc or mingw-w64 -> build/win/ccxview.exe
make wasm            # browser build with Emscripten -> build/web/ccxview.html
build.bat            # Windows with MSVC (no console window; "build.bat console" for one)
make test            # unit tests
make uitest          # the interface clicked through by a script (src/ui_test.c); needs a display or Xvfb
make bench           # the timing tool, build/bench
scripts/pack-binaries.sh   # release archives in dist/
```

Add `-j8` to compile in parallel; every source is its own object, so a rebuild
compiles only what changed. `ccxview --version` prints the version from
[VERSION](../VERSION).

Dependencies: a C compiler and, on Linux, X11 and OpenGL development headers.
Everything else is vendored (sokol, Nuklear, stb_image_write, stb_sprintf,
TinyExpr, miniz, minih264e, minimp4, tinyfiledialogs), and the UI fonts are
compiled in (Inter, Noto Sans Math, Lucide icons; regenerate with
`scripts/embed-fonts.py`). Licences: [vendor/README.md](../vendor/README.md).

## Releasing

Change the number in `VERSION` (e.g. `0.2.0`), commit and push to master. GitHub
Actions ([release.yml](../.github/workflows/release.yml)) then builds Linux,
Windows (mingw-w64) and the web in one job, runs the unit tests and the
interface test, creates the release `v0.2.0` with the three archives and
generated notes, and updates the browser version on GitHub Pages. Other pushes
build nothing. To build without releasing, run the workflow by hand from the
Actions tab (it keeps the archives as artifacts for a week).

The released binaries carry no debug information. A crash report
(`ccxview-crash.txt`) is read with `scripts/symbolize.sh` and a local build of
the same version.

## Tests

- `make test`: the headless modules (readers, mesh, fields, units, loads per
  step, the clip cut, exports), with the address and undefined-behaviour
  sanitizers.
- `make uitest` (`scripts/ui-test.sh`): the real program driven by mouse events
  through its own event handler; checks that lists open, panels answer after
  every window is opened and closed, the wheel and drags do what they should.
  A failed case saves a picture to `build/ui-test/`.
- `make fuzz`: byte-flips every sample file through every reader.

## Timing

`build/bench file.frd` prints what each stage takes. On one desktop PC:

| Model | Read | Surface | Clip cut, per position |
|---|---|---|---|
| 5.0 M elements, 800 MB binary `.frd` | 0.45 s | 0.89 s | 12 ms |
| 1.0 M elements, 426 MB ASCII `.frd` | 0.62 s | 0.21 s | 3 ms |

`make samples` writes these two models (`build/gen_frd`).

## Samples

- `samples/showcase/`: the plate with a hole preloaded in the browser build.
- `samples/elements/`: one small solved deck with every element type and feature.
- `samples/symbols/`: one tile per support, load, thermal load and constraint symbol.
- `samples/vessel/`: a pressure vessel for the stress linearization.
- `samples/cantilever/`: a cantilever under bending, torsion, both, and pressure,
  four steps for the tensor glyphs (`scripts/solve_showcase.sh cantilever`).

Regenerate and solve one with `scripts/solve_showcase.sh NAME` (needs `ccx`).

Design notes: [spec.md](spec.md). Keywords read from a deck: [keywords.md](keywords.md).
