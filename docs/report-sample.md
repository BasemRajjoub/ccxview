# Model summary

The model definition only, as ccxview reads it from the files; numbers are in the units of the input.

## 1. General

| | |
|---|---|
| Input deck | report_demo.inp |
| Heading | Report demo: plate with a stiffener beam, a pipe and a block |
| Units | SI (mm, MPa): mm, t, s  ->  N, MPa |

Analysis steps

| Step | Procedure | Time period | NLGEOM | Initial increment | Min increment | Max increment |
|---|---|---|---|---|---|---|
| 1 | Static | 1 | no | 0.1 | 1e-05 | 0.5 |
| 2 | Static | 1 | yes | 0.05 | 1e-06 | 0.2 |

Solver controls (*CONTROLS, *SOLVER), output requests, contact and other step options: not read.

## 2. Mesh

Element sets

| Set | Elements |
|---|---|
| PLATE | 2 |
| SKIN | 1 |
| RECTBEAM | 1 |
| PIPE | 2 |
| BLOCK | 1 |
| SHELLS | 3 |

Node sets

| Set | Nodes |
|---|---|
| NLEFT | 4 |
| NTIP | 1 |

## 3. Materials

| Material | Elastic model | Constants | First yield stress (*PLASTIC) | Elements |
|---|---|---|---|---|
| STEEL | isotropic | E = 210000, nu = 0.3 | 355 | 0 |
| ALU | isotropic | E = 70000, nu = 0.33 | 240 | 0 |

Other properties (first row of each table; "table": more than one row, temperature dependent)

| Material | Density | Conductivity | Expansion | Expansion zero | Specific heat |
|---|---|---|---|---|---|
| STEEL | 7.85e-09 | 50 | 1.2e-05 | 20 | 4.6e+08 |
| ALU | 2.7e-09 (table) | 160 (table) | 2.3e-05 (table) | - | 9e+08 |

Not read: plasticity beyond the first yield stress, hardening, creep, other constitutive keywords, and conductivity / expansion terms past the first value of orthotropic tables.

## 4. Sections and orientations

| Type | Element set | Material | Elements | Orientation |
|---|---|---|---|---|
| shell | PLATE | STEEL | 2 | - |
| beam | RECTBEAM | STEEL | 1 | - |
| beam | PIPE | ALU | 2 | - |
| solid | BLOCK | ALU | 1 | - |

Shell, membrane and beam sections

| Type | Element set | Material | Shape | Thickness | Offset | Dimensions | Direction |
|---|---|---|---|---|---|---|---|
| shell | PLATE | STEEL | - | 2.5 | 0.5 | - | - |
| shell | SKIN | - | - | composite: 3 layers, total 1.8 | - | - | - |
| beam | RECTBEAM | STEEL | RECT | - | - | 10, 20 | 0, 0, 1 |
| beam | PIPE | ALU | PIPE | - | - | 12, 2 | 0, 1, 0 |

Shell offset is in thicknesses. Beam dimensions are the first data line as given (RECT: a, b; CIRC: r; PIPE: r, t; BOX: a, b, t1..t4; I: l, h, b1, b2, t1, t2, t3; GENERAL: A, I11, I12, I22, It); the direction is the second line (the approximate 1-direction n1). Integration points, composite layer details, other section keywords: not read.

## 5. Boundary conditions and loads

Values as given in the deck, per step (a step keeps those of the steps before unless OP=NEW).

### Step 1

Constraints (*BOUNDARY)

| DOFs | Value | Amplitude | Nodes | Node set / node ids |
|---|---|---|---|---|
| 1-3 | 0 (held) | - | 4 | set NLEFT |

Concentrated loads (*CLOAD, *CFLUX)

| DOF | Magnitude | Amplitude | Nodes | Total | Node set / node ids |
|---|---|---|---|---|---|
| 3 | -100 | RAMP | 1 | -100 | 17 |

Distributed loads on element faces (*DLOAD, *DSLOAD, *DFLUX, *FILM, *RADIATE)

| Type | Value | Amplitude | Faces | Element set / surface / element ids |
|---|---|---|---|---|
| pressure | 2.5 | - | 2 | surface TOP |

### Step 2

Concentrated loads (*CLOAD, *CFLUX)

| DOF | Magnitude | Amplitude | Nodes | Total | Node set / node ids |
|---|---|---|---|---|---|
| 3 | -400 | - | 1 | -400 | 17 |

Distributed loads on element faces (*DLOAD, *DSLOAD, *DFLUX, *FILM, *RADIATE)

| Type | Value | Amplitude | Faces | Element set / surface / element ids |
|---|---|---|---|---|
| pressure | 10 | - | 2 | surface TOP |

### Amplitudes

| Name | Definition | Points | Time |
|---|---|---|---|
| RAMP | TABULAR | 2 | step |

## 6. Constraints and interactions

| Kind | Name | Reference node | Nodes | Slave surface | Master surface | Friction |
|---|---|---|---|---|---|---|
| kinematic coupling | CPL | 19 | via surface | TOP (2 faces) | - | - |

Contact formulation, normal behaviour and friction beyond the coefficient: not read.

Surfaces

| Surface | Type | Faces | Nodes |
|---|---|---|---|
| TOP | element | 2 | 0 |

