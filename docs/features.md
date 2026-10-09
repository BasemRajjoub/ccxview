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
- `.stl` geometry (binary and ASCII) imported beside the results: see below.

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
- Shell section forces, the field `SHELL` beside `STRESS` when a deck with
  shells lies beside the `.frd`: CalculiX expands a shell into a solid (S3, S4,
  S6, S8 into C3D6, C3D8, C3D15, C3D20, a composite into one per layer), so the
  file holds stresses where a structural program shows forces. ccxview turns
  them back, per unit width: `Nxx Nyy Nxy` (membrane), `Mxx Myy Mxy` (bending),
  `Qx Qy` (transverse shear), in the shell's axes as CalculiX's: the element's
  `*ORIENTATION`, else the global x laid on the shell; a composite the
  orientation its layers share, else the global x. Each line of nodes through
  the thickness is integrated on its own, the stress taken linear between two
  nodes and quadratic through three (the corners of a quadratic shell), over
  the layers of a composite, then averaged at the nodes and shown on the whole
  expanded shell. `N = integral of s dz`, `M = integral of s z dz`, `Q =
  integral of the transverse shear dz`, z along the shell normal (e3) from the
  middle of the section, so `Mxx` is the moment of `sxx`, positive when the
  side the normal points to pulls (not the moment turning about x); `Mxy` is
  that of `sxy`. Units are a force and a moment per width (N/mm, N mm/mm), in
  the units window like any quantity. History, CSV, labels and formulas
  (`SHELL_MXX`, `SHELL_NXX / 2`) work on it as on a field of the file.
  Caveats: Qx and Qy come from the solid's nodal transverse shears, which the
  expansion gives only roughly (the stress is extrapolated to the faces, where
  it should be zero); a composite's layers share the nodes between them, where
  CalculiX averages their stresses, so a jump in stiffness is smoothed. The
  sample `samples/shellplate` (an S8R cantilever plate, 100 x 20 x 2 mm) gives
  Nxx = 50 N/mm under a 1000 N pull and Mxx = 100.0 N mm/mm at the root under
  20 N at the tip (F L / b), integrated over the width. Reinforcement design
  (Wood-Armer moments) is not done.
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
  elements pass it and the worst element, one click away. Your own limits per
  measure, a verdict (good, warning up to a share you set, poor) and the whole
  table to the clipboard for a report.
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
follow the deformed shape; a bolt's preload sits on its section inside the
bolt, so it is drawn in front of the faces. [keywords.md](keywords.md) lists every keyword read
and what is drawn for it; `samples/symbols/` shows them all.

Loads with an `*AMPLITUDE` are drawn and labelled with their value at the time
of the increment on screen (tabular amplitudes, step or total time). Elements a
`*MODEL CHANGE` removed are hidden in the steps they are out, and left out of
the legend's range. In a submodel the DOFs and faces the global model drives
(`*BOUNDARY, SUBMODEL`, `*DSLOAD, SUBMODEL`) are drawn in blue and labelled.
Groups > "Steps, amplitudes ..." opens the Deck window: the steps with their
procedure and time, the one on screen, what each `*MODEL CHANGE` takes out or
puts back (and "show removed elements"), the submodel's global results and
driven sets, the amplitudes with their points. [results.md](results.md) lists
the result fields and what is done with each; `samples/modelchange/` shows a
model that changes from step to step and a submodel of it.

## Selection

One floating window, Selection (S, the top bar's Select, View > Selection...,
the context menu, or "selection..." in the probe), for everything that picks
elements and nodes:

- Mode: what is picked next replaces the selection, is added to it, removed
  from it, or intersected with it. The mode applies to every way of selecting.
- Takes: elements, nodes or both; only the side facing the camera (nodes on
  faces turned toward it, elements with such a face) or through the model.
  Changing a tick takes the last box again with the new ticks.
- Box, as in CAD (Ctrl+Shift+drag at any time, or the box button): left to
  right the elements wholly inside, right to left every element touched.
- Click: while armed, a click in the view takes the element (and node) under
  the cursor by the mode; with "select" a click on a selected one takes it out.
  Esc puts the tool down.
- Lasso: drawn round what you want with the left button held, the elements
  wholly inside (the nodes inside, when ticked).
- Faces: a click on an outer face takes every face reached from it without
  crossing a feature edge (the outline's crease angle, a change of material or
  element type, an edge of one or three faces): a fillet, a hole's wall, a flat
  side. Their elements and the nodes of the faces.
- Edge chain: a click near a feature edge takes the nodes along it, on through
  smooth turns (up to the crease angle) to a corner: a hole's rim, an edge.
- Part: a click on an element takes every shown element connected to it.
- Invert (what is shown and not selected), elements to their nodes, nodes to
  the elements with every node selected, or with any ("touching").
- Grow and shrink by one layer of neighbours; boundary: the nodes on the
  outside of the selected elements (faces no other selected element shares; a
  shell's free edges) and the elements that have them.
- Filters (the "filters" box): keep what lies in a range of x, y or z, or of
  r, theta and the axial coordinate about an X, Y or Z axis through a point
  (undeformed; an element by its centre); what the field shown puts above or
  below a value, or in its top N % (an element by its highest node, its lowest
  for "below"); one element type or material; the side facing the camera. They
  look through the selection, or through everything shown when nothing is
  selected ("select where"); in add mode what passes anywhere joins, in remove
  mode what passes leaves.
- By name: a deck element or node set, a surface (its elements and the nodes of
  its faces), every element of a type or a material; also from a right click on
  a set or surface in Groups.
- By id: a list as you would write it, "1-100, 205, 300-310"; what is not an id,
  or not in the model, is reported.
- The field's max and min over the selection with their node or element ids
  (over the selected nodes, or the selected elements' nodes; per element over
  the elements), kept up to date when the field or step changes; the probe sits
  on the max.
- What it is: the count, the extremes with their ids, the deck sets it shares
  members with ("EHOLE 40/40").
- What is done with it: hide it, show only it, put the crop box round it or the
  clip plane through its centre; its rows as CSV; its ids as `*ELSET` /
  `*NSET` lines, to the clipboard or to `<model>_<name>.inp` beside the model to
  `*INCLUDE` in a deck (a file of its own: the model's input is never
  touched); labels on it alone; its history over the steps (the Integrals
  window over the selection).
- Kept by name: a list in the window (select, add, remove, rename, forget),
  stored as id ranges in the model's `.ccxview` and back when the model opens.
- `--select SPEC` (repeatable, in order) does the same from the command line.

## Looking at results

- Animation: mode shapes, steady-state phases, deformation cycles, step
  playback. Undeformed ghost, min / max markers.
- Probe a node or element, find by id, history of a node over all steps, path
  plots between nodes or through the wall. Details of the probe: position,
  displacement, every component of the field, the element's material, sets
  and nodes.
- Selection of elements and nodes (the Selection window, below), highlighted,
  with the field's max over it marked (the min too when asked: compression, the
  cold spot) and both in the window, the probe and the details.
- Integrals of the field over a set, at every step (Integrals window: Fields >
  integrate..., right click a set in Groups, the view's context menu, or the
  probe's "integrate..." under a box selection). Over a volume (an element set,
  the selection, everything shown): the volume and the volume integral and
  average of every component and of the invariant shown, so a set's homogenised
  stress and strain <S> = (1/V) ∫ S dV, <E> likewise, come out per step as for
  an RVE. Over a surface (a deck surface, the outer faces of a set or of what is
  shown): the area, the integral and area average, and the force a stress
  carries through the faces, ∫ S n dA (n outward), or the push of a pressure
  (a scalar field or formula), -∫ p n dA. Over nodes: the sum of each component
  (reaction forces RF: the total force) and its moment about a point you give.
  Integrated with each element's shape functions and a Gauss rule exact to degree
  5, through the mid-side nodes of quadratic elements, on the undeformed shape;
  solids only (hex 8/20, wedge 6/15, tet 4/10), which covers shells, beams and
  plane elements as CalculiX expands them into the .frd. Click the plot to go to
  a step; CSV writes `<model>_integral_<set>.csv` (step, time, volume or area,
  integral and average per component). A field CalculiX extrapolates from the
  Gauss points to the nodes integrates with that extrapolation's error: on the
  showcase the stress through the loaded face gives 62 241 of the 64 984 applied
  (the reaction sum and the volume average match the load to 0.01 %).
- ASME VIII-2 stress linearization along a line through the wall: membrane,
  membrane + bending, peak and total at both ends and their largest value on the
  line; bending from the components normal to the line as 5-A.4.1.2 asks, or
  from all six. Several lines in one model: "keep" in the Path window stores the
  line under a name (SCL 1, SCL 2 ... or your own), the list beside it shows a
  kept line again, "forget" drops it; they are saved with the model.
- A locked legend range keeps min / max across steps and components; the values past
  either end in grey to stand out, in the map's end colour to blend in, in a colour
  of yours, or not drawn at all. Fifteen colour maps (Fast, cool-warm, viridis,
  turbo, cividis, plasma, inferno, black body, Kindlmann, heat, warm, cool, rainbow
  and its desaturated form, jet); a white box behind the legend for a page.
- A title block in a corner of the view (View > Display, or View > Title block...),
  so pictures and videos carry it: the deck's heading, the result file, the solver
  and its version, the analysis of the step shown (static, frequency, buckling,
  heat transfer ... from the deck, else the .frd header), the step with its
  increment and time or its mode and frequency, the deformation scale, the units,
  the user and the date (today's, or when the solver wrote the file), and up to
  three free lines (project, company, checked by). A line without data is left
  out. Dragged anywhere like the legend; a right click on it picks its lines.
- The top bar holds what is used all the time: deformation on / off and its scale
  (auto and multiples of it, true scale), animation, fit, all symbol layers in
  one box, the colour map and bands, the field and its component, the standard
  views, orthographic, the label kinds.
- Compare two runs (A minus B).
- Clip plane with the cut filled, crop box, mirror symmetry, cyclic symmetry,
  replicate (rows of copies of a periodic model).
- Labels on the model (Fields > Labels), any mix at once: node or element ids, the field's value at
  nodes or per element, the names of sets, surfaces, couplings and materials, loads
  with their values, supports with their held DOFs, the value or the number of every
  Gauss point drawn, the field's extremes (the n smallest and largest, with the Min / max
  balls at them). Drawn by the GPU at their own
  size, hidden by the model where it is in front, thinned to a spacing nearest
  first with the count shown; size, colours and spacing to taste; the selection
  or the probed element only.
- Measurements as labels: the distance between two nodes with its dx dy dz, the
  angle at the middle one of three, the circle through three (radius, centre and
  the normal of its plane; three nodes on a hole give the hole's radius). Started
  from the probe's "measure" row, the right-click menu on a node, or the
  Measurements window (View, or Fields > Labels); the next clicks give the other
  nodes, the status bar says which, Esc cancels. Each has two values: on the
  undeformed mesh, and deformed by the step's displacement at true scale, whatever
  the scale on screen. The label reads "d 80.0771 -> 80.0696 mm" (undeformed ->
  deformed), or one of them with its parts ("d 80.0771 mm (dx 80.0617 dy -1.5692
  dz 0)", "113.623 deg", "R 20 mm (c 0, 0, 5)"), as the window chooses; a step
  without displacement gives the one value. Their lines (the segment, the legs and
  an arc, the circle and a cross at its centre) move with the shape as drawn. The
  window lists both values of each, deletes one or all, copies them as text and
  saves `<model>_measurements.csv`. Kept by node id: a reload keeps them, another
  model clears them.
- A right-click menu with what fits where you click: the element and node under
  the cursor, the box selection, the view. Hide an element, a material, an
  element type or a set, or show only it; hide or isolate a selection; show all.
- Free flight (G) through the model; the eye can cut what lies just ahead of it
  (the cut filled) or hide whole elements there, to see inside.
- Convergence plot from `.sta` / `.cvg`.
- Groups by element type, material and set; named sets and surfaces from the
  deck, highlighted on the model. An element set is ticked to show only the
  ticked sets, or hidden by the eye before its name while everything else stays
  (to look at what it covers); a hidden set wins over a ticked one, its name is
  dimmed, and "show everything" or Show all brings it back. Hide set in the
  right-click menu does the same.
- Imported geometry (#23): parts of the assembly that were not analysed (a pin, a
  clamp, the housing) added from STL files for a picture of the whole: Groups >
  Imported geometry > Import STL..., View > File > Import STL..., a `.stl` dropped
  on the window, or `--stl`. Each file is a layer with a box to show or hide it, its
  colour, an opacity slider (see-through: the results show behind it, blended over
  the model, back faces then front faces; two see-through layers over each other
  blend in list order, not by depth) and a unit scale (x0.001 .. x1000, for a file
  in metres beside a model in mm). Lit like the faces when Shading is on, with its
  outline at the model's crease angle while Outline is on, cut by the clip plane,
  in the PNG and the videos, taken in by Fit. It never moves with the
  deformation, is not on the mirror and replicate copies, and the probe passes
  through it. The layers belong to the model: a reload keeps them, opening another
  model clears them.

## Post-processing kept with the model

What is set up for a model is kept in a small file beside it, `model.ccxview`
for `model.frd` (a deck opened alone: beside the `.inp`), and comes back the
next time the model is opened ("restored model.ccxview" in the status bar): the
view (camera, step, field and component, a formula, failure or mesh field,
per-element, coordinates, deformation, locked range, clip, crop, mirror,
replicate, cyclic, layers, colour map and bands), units, groups switched off,
element sets ticked and hidden, node sets and surfaces ticked, elements hidden
by hand, the path and the history node, the kept linearization lines, the
named selections, the comparison run, the labels and the symbols. It is written a moment after a change
(not while dragging the camera, not while a file loads), before another file
opens and at quit; only when something changed, so opening a model to look at it
leaves no file behind. Reload and Watch file keep it all: what a reload starts afresh
(sets, paths, kept lines, named selections) is read back from the file.

ccxview never writes the solver's files (a `.frd` may still be being written).
A file from an older run, or one edited by hand, is read for what still fits
(sets, nodes and fields that are gone are skipped). View > File > Forget
post-processing deletes it and opens the model afresh; `--no-sidecar` leaves it
alone for one run, `sidecar = 0` in `ccxview.ini` (or `--opt sidecar=0`) for good.
The browser version keeps none. The keys are listed in [usage.md](usage.md).

## cgx geometry

A `.fbd` shows its points, lines, surfaces and sets. Scripts are evaluated by cgx
when it is installed; the mesh `ELTY` asks for is made even without cgx for
lines, 4-sided surfaces and 6-sided bodies (mapped HE8/HE20, QU4/QU8, TR3/TR6,
BE2/BE3).

## Export

PNG of the view (with a transparent background when asked), MP4 video or PNG
sequence of a deformation cycle or of every step, CSV, VTK for ParaView, and a
view file to reproduce a picture later. All
available from the command line for scripting ([usage.md](usage.md)).

## Settings and crashes

Settings (in sections in `ccxview.ini`, beside the executable), open panel
sections, window size and recent files are remembered. Should the viewer itself
crash, it writes `ccxview-crash.txt` (beside it, else in the temp folder) with
the version, the stack and the last log lines; attach it to an issue.
