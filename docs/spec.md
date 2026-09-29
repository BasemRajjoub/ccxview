# ccxview — design spec (v1)

A tiny, portable, dependency-free viewer for CalculiX `.frd` results, written in C99.

## Goals

- Open any CalculiX `.frd` (ASCII or binary) and show nodes, elements and every
  result field of every step.
- **One unified scene.** Nodes (points), faces and edges are independent layers.
  A field is a *colouring* painted onto whichever layers ask for it — there is no
  separate "model view" and "results view".
- Stay interactive at ~5M elements / 1 GB `.frd` on a mid-range GPU.
- Never crash on bad input: skip what cannot be read, report it, keep going.
- Build with only a C compiler: C standard library + OS APIs + vendored
  single-header libraries (sokol, Nuklear).

Non-goals for v1: `.inp` reading, `.dat`/`.sta`/`.cvg`, text editing, user-defined
groups, box select, clip planes. (v2: `.inp` mesh/sets/BCs; v3: `.dat`/`.sta`/`.cvg`.)

## Stack

| Piece | Choice | Why |
|---|---|---|
| Window/input | `sokol_app.h` | single header, talks to Win32/X11/Cocoa directly |
| GPU | `sokol_gfx.h`, GL 4.1 core everywhere | one GLSL shader set; 4.1 is the macOS ceiling |
| UI | `nuklear.h` + `sokol_nuklear.h` | immediate-mode, ANSI C, single header |
| Threads/mmap | own ~150-line `os.c` | pthreads/mmap on POSIX, Win32 elsewhere |

## Layout

```
src/
  base.h        common macros, growable arrays, message list
  os.c/.h       file mapping, threads, mutex, clock
  frd.c/.h      .frd index pass + lazy field decode           (headless)
  mesh.c/.h     topology, groups, visibility, skin, edges, pick (headless)
  field.c/.h    scalar options, von Mises/magnitude, ranges    (headless)
  vmath.h       tiny vec3/mat4
  render.c/.h   sokol_gfx pipelines, buffers, colormaps
  ui.c/.h       Nuklear panels
  app.c         sokol_app entry, state, loading thread, camera input
  fbd.c/.h      cgx geometry reader: points, lines, surfaces, sets (headless)
  inp.c/.h      .inp reader: mesh, sets, surfaces, supports, loads, discrete
                elements, couplings (headless)
  dat.c/.h      .dat reader: integration-point blocks (headless)
  sta.c/.h      .sta / .cvg readers: increments and iterations (headless)
  gauss.c/.h    integration points and shape functions (headless)
  app_field.c   decoded-field cache, colourings, displacement, vectors, markers, path, compare
  app_cam.c     camera, fit, symmetry copies, picking, find
  app_load.c    background loading, skin job, applying a load, reload, view state, --check
  app_settings.c the ini: window, layers, legend, recent files (cfg.c)
  cfg.c/.h      flat INI reader / writer (headless)
  export.c/.h   CSV / legacy VTK writers (headless)
  path.c/.h     Dijkstra over the skin edges (headless)
  log.c/.h      log file, verbose, crash report
  app_deck.c    deck sets, highlights, support / load / spring / coupling glyphs
  app_gauss.c   Gauss point layer from the .dat
  gpu.c/.h      renderer name, GPU busy/load for the title bar, software fallback
  cgx.c/.h      run cgx -bg on a script in a temporary copy of its folder
  app_fbd.c     cgx geometry layers + cgx sets as a display group
  sokol_impl.c  the one TU that compiles sokol + Nuklear implementations
tests/
  test_main.c   headless unit tests (tiny CHECK harness)
  gen_frd.c     synthetic N-element .frd generator (ASCII or binary)
  bench.c       headless parse + skin timing on a file
```

Headless modules (`frd`, `mesh`, `field`) never include sokol or Nuklear.

## Data model

- Nodes: `xyz[3N]` float, `id[N]`. id→index: flat table when ids are dense
  (max id ≤ 4N+1024), otherwise an open-addressing hash.
- Elements (CSR): `off[E+1]`, `conn[]` (dense node indices), `type[E]` (FRD code),
  `mat[E]`, `grp[E]` (compact indices into the distinct values).
- Steps: `(step, inc, time)` + a list of field descriptors
  `{name, component names, byte offset, format}`. **Values are not read in the
  index pass** — a field is decoded when it is shown. Decoded fields live in a small
  cache (last 6 decoded (step, field) pairs).
- Missing node values are NaN and draw as neutral grey ("no data"), never as zero.

## Scene / layers

- **Faces** (exterior surface), **Edges** (edges of exterior faces), **Nodes**
  (nodes of visible elements). Each: visible tick + colour mode *field* / *solid*.
  Defaults: faces=field, edges=solid, nodes off.
- **Groups** (automatic, from the file): by element type, by material number, by
  FRD group number. An element is visible iff its value is ticked on **every** axis
  (AND across axes, OR within an axis).
- **One active field**: (field name, component | magnitude | von Mises) at the
  current step. Nodal (smooth) or element (per-element mean, flat) colouring.
- Skin: faces bucketed by their smallest corner node, matched inside each bucket.
  Rebuilt over visible elements only, on the worker thread.

## Rendering

- Per-node vertex buffers: position, displacement, scalar. Index buffers: skin
  triangles, edge lines, points. Visibility changes rebuild only index buffers.
- Element colouring: per-triangle value in an R32F texture, fetched with
  `gl_PrimitiveID` — no duplicated vertex stream.
- Uniforms: deformation scale, range, band count, colouring mode, lock flag.
  Colormap = 256×1 texture. Changing colour, range or deformation uploads nothing.
- Defaults: colormap *Fast*, 12 bands,
  signed fields centred on zero, out-of-range grey only while the range is locked,
  auto deformation scale = 10 % of the model diagonal, never scaled down below 1.

## Interaction

- Left panel: open box, layers, groups, fields of the current step.
- Toolbar above the view: deform + scale, colormap, bands, range lock, fit, views,
  ortho/perspective. Time bar below: step slider, play, readout.
- Legend on the right. Status bar: file, counts, warnings badge → message list.
- Mouse: left-drag orbit, right/middle-drag pan, wheel zoom, click = probe
  (ray-cast against the skin → element + nearest node + value). Orbit turns
  about the grabbed surface point and the wheel zooms toward the cursor (both
  optional); the turntable axis is world Y or Z.
- Open: command-line argument, drag & drop, or the path box.

## Errors

- Bad record → skipped, message with line number (ASCII) or byte offset (binary).
  Capped at 500 messages. Truncated file → keep what was complete, report once.
- Header counts are capped by the bytes left in the file (no huge allocations).
- Unknown element type / wrong node count / reference to a missing node → element
  dropped, counted, reported once per kind.
- Allocation failure while loading → load fails with a message; app keeps running.

## Testing

- `make test`: headless unit tests (ASCII/binary parse, fixed-column negatives,
  continuation lines, pseudo "ALL" components, truncation, corruption, skin counts,
  group filter, von Mises, magnitude).
- `make fuzz`: byte-flips real `.frd` files; must never crash.
- `make corpus`: parse every `.frd` under `$CCX_EXAMPLES`.
- `tests/gen_frd N` + `bench`: synthetic 5M-element model, timed.
