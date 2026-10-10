# Results ccxview reads

What ccxview does with each result block of a CalculiX `.frd` and with the
`.dat` file. The input deck is in [keywords.md](keywords.md).

## How a `.frd` block is read

Every block is read, whatever its name: ASCII and binary (float or double),
up to 32 components (continuation lines of blocks with more than six), a node
without a value is no value (NaN). The name of a block only matters for the
few things listed under "By name" below; everything else follows from the
number of components and their names:

| The block has | What is offered |
|---|---|
| 3 components (any name) | the magnitude `\|NAME\|`, the components, arrows (Layers > Vectors), the components in a cylindrical system (r, t, a) |
| 6 components named `..xx ..yy ..zz ..xy ..yz ..zx` | a tensor: the principal values (`S1 S2 S3` when the names start with S, `E1 E2 E3` with E, else `P1..P3`), the components, glyphs, principal directions, stress trajectories |
| 6 such components and the name `STRESS..`, `ZZS..`, `STRPOS`, `STRNEG`, `STRMID` | von Mises as well, the first choice of the list |
| anything else | its components as they are |

The component type flags of the `-5` lines are not read, and the "ALL"
pseudo-component is dropped (DISP, FORC, FLUX: written as 4, read as 3).
A FEMaster stress block written `xx yy zz yz zx xy` is put in the usual order.

Calculated fields (Fields > Formula) take any block by name: `NAME_MAG` of a
2- or 3-component block, `NAME_MISES` and `NAME_P1..P3` of any tensor (strains
too), and the shorthands `MISES`, `S1..S3` (of `STRESS`) and `E1..E3` (of
`TOSTRAIN`). Tresca, maximum shear, triaxiality and an equivalent strain are
not built in: write them as a formula (`S1 - S3`, ...).

## By name

| Block | Written for | What ccxview does with it |
|---|---|---|
| `DISP` | `U` | the deformed shape (Deform, auto scale from the last step that is not a mode); the first field shown; turned to global from a `*TRANSFORM` |
| `DISPI` | `U`, steady state dynamics | the imaginary part: the animation turns the shape through its phase (DISP cos wt - DISPI sin wt); no phase slider |
| `PDISP`, `MDISP` | `PU` | components as written: the amplitudes (`MAG1` .. `MAG3`) are lengths for the units, the phases (`PHA1` .. `PHA3`, degrees) are not converted |
| `STRESS`, `STRESSI` | `S` | von Mises, principal values, glyphs, trajectories, failure criteria, stress linearization; turned to global from an `*ORIENTATION` with `GLOBAL=NO` |
| `PSTRESS` | `PHS` | components (the amplitudes are stresses for the units, the phases stay in degrees); written in local systems it cannot be turned back and shows no value |
| `STRPOS`, `STRNEG`, `STRMID` | shell faces (FEMaster) | as `STRESS` |
| `ZZSTR`, `ZZSTRI` | `ZZS` | von Mises, principal values (not turned to global) |
| `TOSTRAIN`, `TOSTRAII`, `MESTRAIN`, `MESTRAII` | `E`, `ME` | principal values, glyphs; turned to global with `GLOBAL=NO` |
| `PE`, `PEEQ` | `PEEQ` | a scalar, a strain for the units |
| `ENER` | `ENER` | a scalar, an energy density for the units |
| `FORC`, `FORCI` | `RF` | magnitude, arrows; turned to global from a `*TRANSFORM` (`FORCI` follows the request of `U`, as CalculiX does) |
| `NDTEMP` | `NT` | a scalar; the first field shown when there is no `DISP` or `STRESS`; temperature units with their offset |
| `FLUX` | `HFL` | magnitude, arrows, heat flux units; turned to global with `GLOBAL=NO` |
| `RFL` | `RFL` | a scalar, a power for the units |
| `ERROR`, `HERROR` | `ERR`, `HER` | the error estimate as written |
| `CONTACT` | `CDIS`, `CSTR` | `COPEN CSLIP1 CSLIP2 CPRESS CSHEAR1 CSHEAR2` as components: the first three lengths, the others stresses for the units. Written for the slave nodes of the active contact elements only: they colour the slave surface, the rest has no value (grey). `COPEN` is negative where the surfaces overlap (its range is never made symmetric about 0: an overclosure of microns and gaps of tenths would share a colour); `CPRESS` is -3 at open nodes CalculiX writes (c0 > 0), which makes the range symmetric about 0 while Colours > center at zero is on. A last option, `STATUS`, is the contact status worked out per slave node (features.md, Contact and ties) |
| `VELO`, `V3DF` | `V`, `VF` | magnitude, arrows, velocity units; turned to global from a `*TRANSFORM` |
| `ACC` | `A` | magnitude, arrows, acceleration units |
| `SDV` | `SDV` | the state variables as components |
| `PS3DF`, `PT3DF`, `TS3DF`, `TT3DF`, `MAXU`, `MAXS`, `PRESS`, `MF` | fluid, maxima | components, with their units |
| `CELS`, `PCONTAC`, `DEPTH`, `HCRIT`, `SEN`, `THSTRAIN`, ... | | components as written, no units |

"Turned to global" needs the deck beside the results: see
[Results in local systems](keywords.md#results-in-local-systems).

## Contact elements and warning node sets

`jobname.cel` (`*NODE FILE, CONTACT ELEMENTS`): read beside the model, or opened
with it. As ccx 2.22 writes it: an `*ELEMENT, TYPE=..., ELSET=contactelements_st<step>_in<increment>_at<attempt>_it<iteration>`
card per element, its id running on past the model's, its nodes the model's (no
`*NODE` block):

| ccx writes | for | read as |
|---|---|---|
| `C3D6  m1 s m2 m4 s m3` | node to surface, a quadrilateral master face (corners only when quadratic) | the slave node `s` (twice), the face `m1 m2 m3 m4` |
| `C3D4  m1 m2 m3 s` | node to surface, a triangular master face | the slave node, the face |
| `C3D8  m1 m2 m3 m4 s1 s2 s3 s4` | surface to surface (a triangle repeats its last corner) | the master face, the slave face; written once per integration point of the slave face, so the same element many times: drawn once, counted once |

The iterations are matched to the `.frd` by step and increment (its `1PSTEP`
record): the increment on screen shows the last iteration of its last attempt.
For the gap a slave node is projected onto the face `m1 m2 m3 (m4)` (a bilinear
quadrilateral, a flat triangle), its normal by the right-hand rule of that order
(outward, as CalculiX orders a master face); with c0 left at its default ccx
writes elements for the closed slave nodes only, with c0 > 0 for every slave node
within reach, open or closed.
Other element types in the file are skipped, with a count in the Messages window.

`jobname_WarnNode*.nam`: `*NSET` files ccx writes for its warnings, read beside
the model with the `.inp` reader. Those named `...Miss...` list slave nodes not
tied (`WarnNodeMissTiedContact` in ccx 2.22; `WarnNodeMissMasterIntersect`
by its name): drawn yellow on the ties. The Contact window lists every one read
with its count.

## Steps

Each increment of the `.frd` is a step on the time bar, with its time; a
`*FREQUENCY` or `*BUCKLE` mode is marked modal (its time is the frequency, the
scale of the shape arbitrary). History (a node or an element over all steps)
plots against the time, or the step number when the time does not grow.
The deck's supports and loads follow the step on screen (the `.frd` numbers its
steps as the deck's `*STEP`s), loads with an `*AMPLITUDE` at the time of the
increment, and elements a `*MODEL CHANGE` removed are hidden.

## Cyclic symmetry, comparisons

The cyclic view draws the sector again N times about the axis of
`*CYCLIC SYMMETRY MODEL`: the same values in every copy, vector and tensor
components not turned; there is no expansion by nodal diameter. A second run
(Compare) is subtracted node by node, step by step.

## The `.dat` file

Read: the blocks of integration-point values, those with `integ.pnt.` in their
header (`*EL PRINT`: stresses, strains, plastic strain, energies per point,
...), each with its set, step and time, and the `global coordinates` block for
the positions of the points. Shown at the integration points (Layers > Gauss
pts) for the increment of the same time; a 3-component block offers its
magnitude, a stress block von Mises and principal values, a strain block
principal values; records CalculiX printed in a local system (`*EL PRINT`'s
default `GLOBAL=NO`) are turned to global exactly. Formulas, glyphs and arrows
work on `.frd` fields only.

Not read: the nodal prints (`*NODE PRINT`: displacements, forces, temperatures),
`total force`, and element totals without integration points (`ELSE`, `EVOL`,
`EMAS`, ...).

The samples hold `DISP`, `STRESS`, `TOSTRAIN`, `FORC`, `ERROR` (all of them),
`CONTACT` (elements, contact), `NDTEMP`, `FLUX` (symbols) and `.dat` stresses
(showcase, elements); a `.cel` (contact) and a warning `.nam` (tie).
