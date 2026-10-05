# Features

What ccxview does, in full. The short version is in the [README](../README.md).

## Speed

- A 5 million element model (800 MB binary `.frd`) is read in under half a
  second and its surface built in under one; see [build.md](build.md#timing) for
  the numbers and how to measure your own.
- Fields are decoded only when shown, so a file with hundreds of steps opens as
  fast as one with a single step.
- Symbols, vector arrows and Gauss points are drawn by GPU instancing and
  impostors: one stored shape, a few numbers per item.
- The clip plane's filled cut is prepared once per direction; moving it costs
  about 10 ms on 5 million elements.
- Renders on OpenGL 4.1 when a driver exists; otherwise ccxview restarts itself
  on Mesa's software renderer. The browser build runs on WebGL2.

## Files

- `.frd` results (ASCII and binary), `.dat` integration-point output, `.sta` /
  `.cvg` convergence, `.inp` decks, cgx `.fbd` geometry.
- A `.inp` opens on its own: mesh, sets, surfaces, materials, supports and
  loads. With the `.frd` beside it, both are shown together.
- Results of other solvers that write `.frd` (FEMaster) are read too, including
  their tensor order and shell top / bottom fields.
- Damaged records are skipped and listed in the message window, never a crash.

## Elements

Hex, tet, wedge, shells, plane and axisymmetric elements, beams, trusses,
springs, dashpots, masses, gaps, rigid bodies, couplings, ties and contact
pairs. Quadratic elements are drawn through their mid-side nodes, so the colours
and the shape follow every node; the full list is in
[keywords.md](keywords.md#element-types).

## Fields

- Any result component as colour on faces, edges or nodes; per-element (flat)
  colouring; vectors and principal directions as 3D arrows.
- Stress and strain tensors as a glyph per element, six styles:
  ellipsoids (semi-axes |s1| |s2| |s3| along the principal directions);
  superquadrics (Kindlmann 2004: edged where two values are close, so rods,
  discs and balls tell apart from any side); the principal cross (a bar per
  value, heads out for tension, in for compression); Schultz-Kindlmann
  superquadrics (2010: any signs, mixed signs pinch the shape); Reynolds glyphs
  (the normal stress on every plane) and HWY glyphs (Hashash, Yao, Wotring: the
  shear stress on every plane). Coloured by the field, or by the sign (blue
  compression, red tension, with a key under the legend). Sized from the
  elements, moving with the deformed shape.
- Principal stress trajectories: evenly spaced curves along S1 and / or S3
  through the solid, the load paths, coloured by the principal value.
- Gauss points as balls at the integration points, coloured by the field, or by
  the exact values of a `.dat` file.
- Principal stresses, von Mises, magnitudes; cylindrical coordinate systems.
- Results CalculiX wrote in local systems (`*TRANSFORM`, `*ORIENTATION`, shells)
  are turned back to global with the deck beside them.
- Units: say what the file was written in, choose what to see, per quantity.
  Values are converted, not relabelled.
- Calculated fields: a formula over the results, shown like any field, with its
  history and exports: `S1 - S3` (Tresca), `MISES / 235`, `sqrt(D1^2 + D2^2)`,
  `if(MISES > 200, 1, 0)`. Components, magnitudes, von Mises and principal
  values by name, node coordinates `X Y Z`, the step `TIME`, and the usual maths
  functions.
- Failure criteria as a field, from the stresses in each element's material
  axes (composite layers, `*ORIENTATION`, cylindrical systems): maximum stress,
  Tsai-Hill, Tsai-Wu, Hashin, Puck (action plane), LaRC03 and LaRC05 for UD
  plies; von Mises, Tresca and Mohr-Coulomb for isotropic materials; Auto, the
  default, picks per material (LaRC05 for plies, von Mises for metals and
  plastics, Mohr-Coulomb for brittle ones), and any other criterion judges the
  materials of the other kind by theirs. Shown as
  exposure, reserve factor, failure index, governing mode, fracture plane angle,
  or fibre and matrix exposure apart; the probe names the mode. LaRC in-situ
  strengths follow the ply's thickness and place in the laminate.
- Mesh quality as a field, per element on the undeformed mesh: size (volume,
  area, length), shortest and longest edge, aspect ratio, scaled Jacobian,
  Jacobian ratio (through the mid-side nodes), equiangle skewness, smallest and
  largest face angle, warpage, shape factor. The Mesh quality window sums up the
  mesh (element types, nodes, materials, volume, extent) and gives each measure
  its range, mean, spread, the usual limit (Abaqus, ANSYS, Verdict), how many
  elements pass it and the worst element, one click away.
- Overall mesh scores, each made the way its program makes it and explained in
  the window: ccxview quality (0 to 1, the weakest of the measures, each scored
  from its ideal to its limit), the HyperMesh quality index (penalties 0 at
  good, 1 at fail, 10 at worst), ANSYS Element Quality (C V / sqrt(sum e^2)^3)
  and the number of Abaqus Verify Mesh checks failed. The probe names the
  measure that sets the ccxview score.
- Strength materials: Xt, Xc, Yt, Yc, S12, S23, Puck inclinations, fracture
  toughness, yield and ultimate strengths. Templates of well documented
  materials (the WWFE plies, IM7/8552, T800S/M21, AS4/PEEK, structural,
  stainless and quenched and tempered steels, aluminium and titanium alloys,
  grey cast iron, PEEK, PA66, PC, POM, ABS, PP), each with its source; elastic
  constants taken from the deck. Kept in the settings file, assigned per deck
  material. A deck material with none assigned takes the nearest template on
  its own, by its name (`S355`, `IM7/8552`, `PA66`, `steel`, `CFRP`...) or else
  its `*ELASTIC` data, with the deck's E, nu and `*PLASTIC` yield stress, so the
  field shows something from the start; the legend says which.

## The deck on the model

Supports, loads, thermal loads and constraints of the step on screen, each kind
with a symbol of its own: held and prescribed degrees of freedom, forces,
moments, pressures, edge loads, gravity, centrifugal load, bolt preload, heat
flux, film, radiation, given temperatures, springs, dashpots, masses, gaps,
couplings, equations and MPCs. Symbols are solid, sized from the model, and
follow the deformed shape. [keywords.md](keywords.md) lists every keyword read
and what is drawn for it; `samples/symbols/` shows them all.

## Looking at results

- Animation: mode shapes, steady-state phases, deformation cycles, step
  playback. Undeformed ghost, min / max markers.
- Probe a node or element, find by id, history of a node over all steps, path
  plots between nodes or through the wall.
- ASME VIII-2 stress linearization along a line through the wall: membrane,
  membrane + bending, peak and total at both ends and their largest value on the
  line; bending from the components normal to the line as 5-A.4.1.2 asks, or
  from all six.
- Compare two runs (A minus B).
- Clip plane with the cut filled, crop box, mirror symmetry, cyclic symmetry,
  replicate (rows of copies of a periodic model).
- Convergence plot from `.sta` / `.cvg`.
- Groups by element type, material and set; named sets and surfaces from the
  deck, highlighted on the model.

## cgx geometry

A `.fbd` shows its points, lines, surfaces and sets. Scripts are evaluated by cgx
when it is installed; the mesh `ELTY` asks for is made even without cgx for
lines, 4-sided surfaces and 6-sided bodies (mapped HE8/HE20, QU4/QU8, TR3/TR6,
BE2/BE3).

## Export

PNG of the view, MP4 video or PNG sequence of a deformation cycle or of every
step, CSV, VTK for ParaView, and a view file to reproduce a picture later. All
available from the command line for scripting ([usage.md](usage.md)).

## Settings and crashes

Settings (in sections in `ccxview.ini`, beside the executable), open panel
sections, window size and recent files are remembered. Should the viewer itself
crash, it writes `ccxview-crash.txt` (beside it, else in the temp folder) with
the version, the stack and the last log lines; attach it to an issue.
