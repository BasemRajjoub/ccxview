# ccxview

A fast viewer for CalculiX results and models: `.frd`, `.inp`, `.dat` and cgx
`.fbd`. One small executable, no installation.

![demo](docs/demo.gif)

**Try it in the browser, nothing to install:** https://basemrajjoub.github.io/ccxview/

Download the [latest release](https://github.com/BasemRajjoub/ccxview/releases/latest):
[Linux](https://github.com/BasemRajjoub/ccxview/releases/latest/download/ccxview-linux-x86_64.tar.gz) ·
[Windows](https://github.com/BasemRajjoub/ccxview/releases/latest/download/ccxview-windows-x86_64.zip) ·
[Web, one HTML file](https://github.com/BasemRajjoub/ccxview/releases/latest/download/ccxview-web.zip)

## Fast

| Model | Read | Surface built |
|---|---|---|
| 5 million elements, 800 MB `.frd` | 0.45 s | 0.9 s |
| 1 million elements, 426 MB ASCII `.frd` | 0.6 s | 0.2 s |

- Fields are decoded only when shown: a file with hundreds of steps opens as fast as one with a single step.
- Stays smooth on millions of elements: everything is drawn on the GPU.
- Moving a filled clip plane through 5 million elements: 12 ms.
- About 2 MB or less to download. Runs without a GPU too, and in the browser.

Measured on one desktop PC; `build/bench file.frd` prints the numbers for yours.

## What you get

- **Results as you expect them.** Contours on faces, edges and nodes, deformed
  shape, animation, vectors, principal stresses, tensor glyphs, stress
  trajectories, Gauss points, probe with full node and element details,
  CAD-style box selection with its max and min, history and path plots.
  Labels on the model: ids, values, the n largest and smallest, set and
  material names, loads and supports, any mix, thinned to stay readable.
- **The deck on the model.** Supports, loads, thermal loads, bolts, springs and
  constraints of the step on screen, each with its own 3D symbol.
- **True to the element.** Quadratic elements are drawn and cut through their
  mid-side nodes; shells and beams as CalculiX expands them.
- **Engineering tools.** ASME VIII-2 stress linearization, shell section
  forces and moments per width (Nxx .. Mxy, Qx Qy) from the expanded shells' stresses
  and the reinforcement of concrete shells from them (sandwich model, Wood-Armer), calculated fields
  from a formula (`S1 - S3`, `MISES / 235`), composite and metal failure
  criteria (Hashin, Puck, LaRC05, Tsai-Wu, von Mises, ...) with built-in
  composite, metal and plastic presets, mesh quality
  (aspect ratio, Jacobians, skew, warpage, ...) with your own limits and a
  report, unit conversion,
  comparison of two runs, cylindrical systems.
- **Cut and copy.** Clip plane with a filled cut, crop box, mirror, cyclic
  symmetry, replicate. Fly through the model: the eye cuts its way in, or
  hides the elements in front of it.
- **Export.** PNG, MP4, CSV, VTK, all scriptable from the command line.

```sh
ccxview model.frd
ccxview model.frd --shot out.png
ccxview model.frd --calc "S1 - S3" --export mp4
```

## More

- [Features in full](docs/features.md)
- [Command line, mouse and keys](docs/usage.md)
- [Deck keywords and symbols](docs/keywords.md)
- [Result fields and what is done with them](docs/results.md)
- [Building, testing, releasing, timings](docs/build.md)
- [Design notes](docs/spec.md)

GPL-3.0-or-later. Vendored libraries and fonts keep their own licences:
[vendor/README.md](vendor/README.md).
