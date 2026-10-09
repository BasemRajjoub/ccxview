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
| `PDISP`, `MDISP` | `PU` | components as written (every component counted a length for the units) |
| `STRESS`, `STRESSI` | `S` | von Mises, principal values, glyphs, trajectories, failure criteria, stress linearization; turned to global from an `*ORIENTATION` with `GLOBAL=NO` |
| `PSTRESS` | `PHS` | components; written in local systems it cannot be turned back and shows no value |
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
| `CONTACT` | `CDIS`, `CSTR` | `COPEN CSLIP1 CSLIP2 CPRESS CSHEAR1 CSHEAR2` as components: the first three lengths, the others stresses for the units |
| `VELO`, `V3DF` | `V`, `VF` | magnitude, arrows, velocity units; turned to global from a `*TRANSFORM` |
| `ACC` | `A` | magnitude, arrows, acceleration units |
| `SDV` | `SDV` | the state variables as components |
| `PS3DF`, `PT3DF`, `TS3DF`, `TT3DF`, `MAXU`, `MAXS`, `PRESS`, `MF` | fluid, maxima | components, with their units |
| `CELS`, `PCONTAC`, `DEPTH`, `HCRIT`, `SEN`, `THSTRAIN`, ... | | components as written, no units |

"Turned to global" needs the deck beside the results: see
[Results in local systems](keywords.md#results-in-local-systems).

## Steps

Each increment of the `.frd` is a step on the time bar, with its time; a
`*FREQUENCY` or `*BUCKLE` mode is marked modal (its time is the frequency, the
scale of the shape arbitrary). History (a node or an element over all steps)
plots against the time, or the step number when the time does not grow.
The deck's supports and loads follow the step on screen (the `.frd` numbers its
steps as the deck's `*STEP`s).

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
`CONTACT` (elements), `NDTEMP`, `FLUX` (symbols) and `.dat` stresses
(showcase, elements).
