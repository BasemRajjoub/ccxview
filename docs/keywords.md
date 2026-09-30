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
| `*INCLUDE` | followed, up to 8 levels; the included file may continue the block that was open |
| `*HEADING` | skipped, its lines do not start a block |

`*STEP` boundaries are not tracked: supports and loads from every step are
collected, the last `*CLOAD` on a node and DOF wins.

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
temperatures and fluxes, orientations and transformations, output requests,
contact properties. The results of all of these come back through the `.frd`
and `.dat` files, which ccxview reads in full.
