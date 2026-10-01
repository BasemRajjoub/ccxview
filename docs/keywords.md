# Input deck keywords ccxview reads

ccxview is a viewer, not a solver: it reads an `.inp` to draw the mesh, the
sets and what is applied to them, and to name things in the results. Every
keyword not listed here is skipped without a message. A deck that ccx
accepts always opens; a deck with a keyword ccxview does not know still
opens, it just shows less. New solver keywords (a new material model, a new
step type, `*DAMAGE INITIATION` in ccx 2.23, ...) need no change here.

## Read and shown

| Keyword | What ccxview does with it |
|---|---|
| `*NODE` | coordinates; `NSET=` makes a node set |
| `*ELEMENT` | connectivity, `TYPE=`, `ELSET=`; the element types below |
| `*NSET`, `*ELSET` | named sets, `GENERATE` too; listed under Groups, selectable |
| `*SURFACE` | `TYPE=ELEMENT` (element faces) and `TYPE=NODE`; drawn under Groups > Surfaces |
| `*SOLID SECTION`, `*SHELL SECTION`, `*BEAM SECTION`, ... | `MATERIAL=` per `ELSET=`: the material of each element, for the Material groups |
| `*MATERIAL` | the material's name |
| `*BOUNDARY` | supports: node, first and last DOF; drawn as cones (Layers > Supports) |
| `*CLOAD` | point loads: node, DOF, magnitude; drawn as arrows (Layers > Loads) |
| `*DLOAD` | pressure on element faces (`P1`..`P6`, shell `P`); drawn as arrows on the faces |
| `*SPRING`, `*DASHPOT` | the DOF of a one-node spring or dashpot |
| `*EQUATION` | multi-point constraints, drawn as links between the nodes |
| `*RIGID BODY` | the reference node linked to its `NSET=` or `ELSET=` |
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

For supports and loads `*STEP` boundaries are not tracked: they are collected
from every step, the last `*CLOAD` on a node and DOF wins. Output requests are
tracked per step.

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

## Not read

Materials beyond the name, steps and their controls, amplitudes,
temperatures and fluxes, output requests beyond `GLOBAL=`, contact
properties. The results of all of these come back through the `.frd`
and `.dat` files, which ccxview reads in full.
