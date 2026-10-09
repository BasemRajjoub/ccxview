# Input deck keywords ccxview reads

ccxview is a viewer, not a solver: it reads an `.inp` to draw the mesh, the
sets and what is applied to them, and to name things in the results. Every
keyword not listed here is skipped without a message. A deck that ccx
accepts always opens; a deck with a keyword ccxview does not know still
opens, it just shows less. New solver keywords (a new material model, a new
step type, `*DAMAGE INITIATION` in ccx 2.23, ...) need no change here. What it
does with the results is in [results.md](results.md).

## Read and shown

| Keyword | What ccxview does with it |
|---|---|
| `*NODE` | coordinates; `NSET=` makes a node set |
| `*ELEMENT` | connectivity, `TYPE=`, `ELSET=`; the element types below |
| `*NSET`, `*ELSET` | named sets, `GENERATE` too; listed under Groups, selectable |
| `*SURFACE` | `TYPE=ELEMENT` (element faces) and `TYPE=NODE`; drawn under Groups > Surfaces |
| `*SOLID SECTION`, `*SHELL SECTION`, `*BEAM SECTION`, ... | `MATERIAL=` per `ELSET=`: the material of each element, for the Material groups |
| `*MATERIAL` | the material's name |
| `*BOUNDARY` | supports (Layers > Supports), cyan. A held DOF: a cone per axis with its tip on the node, a second base for a held rotation. A value other than 0: a prescribed displacement, an arrow with a bar across its tail; a prescribed rotation, a turning arrow. DOF 11: a temperature, a small cross |
| `*CLOAD` | point loads (Layers > Loads). Forces (DOF 1-3): yellow arrows. Moments (DOF 4-6): magenta double-headed arrows with an arc that turns the way the moment does |
| `*DLOAD` | `P1`..`P6`, shell `P`: pressure, an arrow on the face. On a plane element (`CPS`, `CPE`, `CAX`) the face is an edge: an arrow with a bar across its tail. `EDNOR1`..`4`: the same on a shell's edge. `GRAV`, `BX`/`BY`/`BZ`: a block arrow leaving the element set. `CENTRIF`: the axis as a dashed line with a turning arc. `NEWTON` is read and not drawn |
| `*DSLOAD` | pressure on a `*SURFACE`: as `*DLOAD` on each of its faces |
| `*PRE-TENSION SECTION` | a bolt: a ring round its `SURFACE=` (or at its beam `ELEMENT=`) with arrows along the preload, meeting at the cut when tightened. The preload is the `*CLOAD` or `*BOUNDARY` on DOF 1 of its `NODE=`, which is not drawn as a force of its own |
| `*CFLUX`, `*DFLUX` | heat into a node or a face (`S1`..`S6`): a red arrow with a zigzag shaft, turned round when it leaves. `BF`: a zigzag block arrow on the element set |
| `*FILM` | convection on a face: a red zigzag ending in a bar |
| `*RADIATE` | radiation from a face: a red stem ending in three rays |
| `*TEMPERATURE` | a temperature given to nodes: red diamonds, labelled `T` |
| `*STEP`, `OP=NEW` | every line above keeps its step. The symbols are those in force in the step on screen (the `.frd` numbers its steps as the deck's `*STEP`s; a deck without results shows its last step): a later value for the same node and DOF, or element face, replaces the earlier one; `OP=NEW` drops all of its kind from earlier steps; supports given before the first step stay |
| `*STATIC`, `*DYNAMIC`, `*HEAT TRANSFER`, `*COUPLED TEMPERATURE-DISPLACEMENT`, `*UNCOUPLED ...`, `*VISCO`, `*MODAL DYNAMIC`, `*ELECTROMAGNETICS` | the procedure of the step and its time period (the second number of the data line, 1 when not given), for the times of the amplitudes. `*FREQUENCY`, `*BUCKLE`, `*STEADY STATE DYNAMICS`, `*COMPLEX FREQUENCY`, `*GREEN`, `*SENSITIVITY` take no time |
| `*AMPLITUDE` | `TABULAR` (the default), in step time or with `TIME=TOTAL TIME` in total time, `SHIFTX=`, `SHIFTY=`: straight between its points, constant beyond the ends. `AMPLITUDE=` on `*BOUNDARY`, `*CLOAD`, `*DLOAD`, `*DSLOAD`, `*CFLUX`, `*DFLUX`, `*FILM`, `*RADIATE`, `*TEMPERATURE`: the symbol and its label show the line's value times the amplitude at the time of the increment on screen (the step's end without results), its length follows. Other definitions (`DEFINITION=SMOOTH STEP`, `PERIODIC`, `USER`, ...) keep their name and are not evaluated: their loads keep the line's value and their labels say "not evaluated". Without an amplitude a load is shown with its value at the step's end, where CalculiX's ramp in a static step ends. The Deck window lists the amplitudes and their points |
| `*MODEL CHANGE` | `TYPE=ELEMENT, REMOVE` / `ADD` with elements or element sets: the elements removed in the step on screen, or before it and not added back, are hidden (their nodes leave the legend's range) and come back on an earlier step; Deck window > "show removed elements" (`--opt show_removed=1`) draws them. `TYPE=CONTACT PAIR`: a removed pair's surfaces are not highlighted in its steps |
| `*SUBMODEL` | `TYPE=NODE` (node sets or nodes) and `TYPE=SURFACE` (surfaces), `INPUT=`, `GLOBAL ELSET=`: listed in the Deck window. `*BOUNDARY, SUBMODEL, STEP=`: the driven DOFs as support cones in blue, labelled `global UX UY UZ`; `*DSLOAD, SUBMODEL`: a blue arrow on each face, labelled with the global step its pressure comes from |
| `*TRANSFORM` | also for symbols: supports, forces, moments and prescribed displacements of a node in a transform point along its local axes |
| `*MPC` | `BEAM`, `PLANE`, `STRAIGHT` and user MPCs: links between their nodes, as `*EQUATION` |
| `*CYCLIC SYMMETRY MODEL` | `N=` and the axis preset the cyclic view (View > Cyclic symmetry) |
| `*SPRING`, `*DASHPOT` | the DOF of a one-node spring or dashpot |
| `*EQUATION` | multi-point constraints, drawn as links between the nodes |
| `*RIGID BODY` | the reference node linked to its `NSET=` or `ELSET=`; on its `ROT NODE=` DOFs 1-3 are the body's rotations, so a `*CLOAD` there is drawn as a moment and a held DOF as a held rotation |
| `*COUPLING` + `*KINEMATIC` / `*DISTRIBUTING` | the reference node linked to its `SURFACE=` |
| `*DISTRIBUTING COUPLING` | the `DCOUP3D` element's node linked to the listed nodes |
| `*TIE` | the slave and master surfaces, drawn as a surface pair |
| `*CONTACT PAIR` | the slave and master surfaces, drawn as a surface pair |
| `*TRANSFORM` | the node system of its `NSET=` (rectangular or cylindrical), to turn local nodal results back (below) |
| `*ORIENTATION` | the element system (rectangular or cylindrical, with the extra rotation line), by `NAME=`; the sections that name it give it to their elements |
| `*SHELL SECTION, COMPOSITE` | the orientation of each layer |
| `*NODE FILE`, `*EL FILE`, `*NODE OUTPUT`, `*ELEMENT OUTPUT` | per `*STEP`: `GLOBAL=` of `U`, `RF`, `V`, `VF`, `S`, `E`, `HFL` -- whether the `.frd` holds them in local systems |
| `*INCLUDE` | followed, up to 8 levels; the included file may continue the block that was open |
| `*HEADING` | skipped, its lines do not start a block |

Symbols of one kind are sized against the largest value of that kind in the
step. Their base size is 2.5 % of the model's diagonal, the same on a coarse and
a fine mesh, or a size in model units set by hand (View > Symbol sizes: auto
size, thickness; `--opt sym_auto=0 --opt sym_size=5 --opt sym_thick=2`). Heads
and supports are solid cones and every stroke a thin tube, so a symbol reads
from any side. They are drawn by GPU instancing (one stored body, 15 numbers per
cone or tube), never thinner than about a pixel, so there is no limit on their
number worth naming; above 30000 bodies of one colour the body has six sides
instead of twelve. Vector arrows (Layers > Vectors) are drawn the same way. `samples/symbols/` holds a deck with one small part per symbol
(`scripts/gen_symbols.py`): open `symbols.frd` and step through it.

## Results in local systems

CalculiX writes some results in local systems and the `.frd` does not say so;
the deck does. With the deck beside the results ccxview turns them back to
global when it decodes them (`cv_localsys` in `inp.h`), following ccx 2.22:

- Nodal values (`DISP`, `FORC`, `VELO`, ...) are in the `*TRANSFORM` of their
  node unless the request says `GLOBAL=YES`. Exact.
- Element values (`STRESS`, `TOSTRAIN`, `MESTRAIN`, `FLUX`) only with
  `GLOBAL=NO`, in the system of the element: its `*ORIENTATION`, and for every
  shell (`S3`..`S8R`) a system of its own even without one (gen3dfrom2d.f:
  normal = e3, the orientation's or the global x projected on the shell = e1).
  CalculiX turns them at each integration point and then averages at the nodes,
  so they come back exactly where every element around a node has the same
  system: rectangular orientations, flat shells, composite layers (each layer
  is an element with nodes of its own). Where the systems differ -- a
  cylindrical orientation, a curved shell, the border of two orientations --
  the values are turned with the mean system while the systems differ by less
  than 12 degrees (error about 0.3% of the peak at 10 degrees per element for
  solids, 1% for shells, growing with the square of the angle), and shown as no
  value beyond. `PSTRESS` written locally cannot be undone and shows no value.
  The message bar says which.
- `.dat` records from `*EL PRINT` (whose default is `GLOBAL=NO`) name their
  system after the values, so they are turned back exactly, every one.

Without the deck the values are shown as written. A comparison `.frd` is not
turned. For exact values everywhere, request `GLOBAL=YES` (the default of
`*NODE FILE` and `*EL FILE`).

## Element types

Solids `C3D4`, `C3D6`, `C3D8`, `C3D8R`, `C3D8I`, `C3D10`, `C3D10T`, `C3D15`,
`C3D20`, `C3D20R` and the fluid `F3D4`/`F3D6`/`F3D8`; shells and plane
elements `S3`, `S4`, `S4R`, `S6`, `S8`, `S8R`, `CPS*`, `CPE*`, `CAX*`, `M3D*`;
beams and trusses `B31`, `B31R`, `B32`, `B32R`, `T2D2`, `T3D2`, `T3D3`; the
discrete `SPRINGA`, `SPRING1`, `SPRING2`, `DASHPOTA`, `DASHPOT1`,
`DASHPOT2`, `MASS`, `GAPUNI`, `DCOUP3D`. Elements whose nodes are missing are
dropped and reported in the message bar.

## Quadratic elements

The faces of quadratic elements (`C3D20`, `C3D15`, `C3D10`, `S8`, `S6`, the solids
CalculiX expands shells and beams into) are drawn through their mid-side nodes: a
quadrilateral as six triangles, a triangle as four, every edge in two pieces. The
colours and the shape then follow every node. It matters most for a shell in
bending: the expanded solid has one element across the wall, and only its mid nodes
carry the neutral plane. Layers > "mid-side nodes" off (`--opt mid_faces=0`) draws
corners only, a third of the triangles, for very large models.

## Not read

Materials beyond the name and the first elastic constants and yield stress,
step controls, `TIME DELAY=` and `LOAD CASE=` of a load (shown as if not
given), `*CLOAD, SUBMODEL` and `*TEMPERATURE, SUBMODEL`, the ramp of a load
inside a static step (shown at the step's end), output requests beyond
`GLOBAL=`, contact properties, user loads (`P1NU`, ...). The results of all of
these come back through the `.frd` and `.dat` files: see
[results.md](results.md).

`samples/modelchange/` (`scripts/gen_modelchange.py`) is a bar propped near its
tip whose prop is removed in step 2 and added back in step 3, loaded through
two amplitudes, and a submodel of its root driven by its displacements (step 1)
and its stresses (step 2).
