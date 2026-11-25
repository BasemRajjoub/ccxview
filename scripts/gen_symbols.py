#!/usr/bin/env python3
"""gen_symbols.py -- write samples/symbols/symbols.inp: a catalogue of everything
ccxview draws from a deck. One small part ("tile") per support, load, thermal load
and constraint, each in its own element set named after what it shows, so the model
is also the test of the symbols: every tile must show its own, and only that.
Solve it with scripts/solve_showcase.sh symbols.

Tiles are 10 mm cubes (or a shell, a beam, a plane element) on a grid, 25 mm apart
along X, one row per family along Y:
  row 0  supports     held, held with rotations, prescribed displacement and rotation,
                      held temperature, supports and a force in a cylindrical *TRANSFORM
  row 1  loads        force, moment, pressure (*DLOAD), surface pressure (*DSLOAD),
                      shell edge load, pressure on a plane element's edge, gravity,
                      centrifugal, body force, two bolts (*PRE-TENSION SECTION; the first
                      held in the last step), a bolt buried in a block
  row 2  thermal      *CFLUX, *DFLUX, *FILM, *RADIATE, *TEMPERATURE, body heat (BF)
  row 3  constraints  rigid body, kinematic and distributing coupling, *EQUATION, *MPC,
                      *TIE, *CONTACT PAIR, spring, dashpot, mass, gap, a rigid body turned
                      by a moment on its ROT NODE
Step 1 (*HEAT TRANSFER) holds the thermal loads; CalculiX keeps them through the later
steps. Step 2 (*STATIC) adds every mechanical load. Step 3 (*STATIC) drops all point
loads (*CLOAD, OP=NEW) but one force, turned round and doubled, and adds a pressure:
the symbols follow the step on screen.
"""
import os

OMIT = set(os.environ.get("SYMBOLS_OMIT", "").split(","))    # debugging: parts to leave out
nodes, elems, lines = {}, [], []
nsets, elsets = {}, {}
nid, eid = [0], [0]
D = 10.0


def node(x, y, z):
    nid[0] += 1
    nodes[nid[0]] = (float(x), float(y), float(z))
    return nid[0]


def elem(t, conn, elset):
    eid[0] += 1
    elems.append((eid[0], t, list(conn), elset))
    elsets.setdefault(elset, []).append(eid[0])
    return eid[0]


def nset(name, ids):
    nsets.setdefault(name, []).extend(ids)
    return name


def at(col, row):
    return 25.0 * col, 30.0 * row, 0.0


def cube(col, row, name, dz=0.0, h=D):
    """C3D8: corners 0..3 bottom (z0), 4..7 top; faces S1 bottom, S2 top, S3 y0, S4 x1, S5 y1, S6 x0"""
    x, y, z = at(col, row)
    z += dz
    c = [node(x, y, z), node(x + D, y, z), node(x + D, y + D, z), node(x, y + D, z),
         node(x, y, z + h), node(x + D, y, z + h), node(x + D, y + D, z + h), node(x, y + D, z + h)]
    e = elem("C3D8", c, name)
    return c, e


def held(ns, lo=1, hi=3):
    lines.append("*BOUNDARY")
    for n in ns:
        lines.append(f"{n}, {lo}, {hi}")


step1, step2, step3 = [], [], []      # the load cards of each step
model = lines                          # model data: sections, constraints, permanent supports

# ---- row 0: supports ---------------------------------------------------------------
c, _ = cube(0, 0, "S1_HELD")
held(c[:4])

x, y, z = at(1, 0)                     # a shell: translations and rotations held
q = [node(x, y, z), node(x + D, y, z), node(x + D, y + D, z), node(x, y + D, z)]
elem("S4", q, "S2_HELD_ROTATIONS")
held(q[:2], 1, 6)
step1 += ["*CLOAD", f"{q[2]}, 3, -2."]

c, _ = cube(2, 0, "S3_PRESCRIBED_DISPLACEMENT")
held(c[:4])
step1 += ["*BOUNDARY"] + [f"{n}, 3, 3, 0.02" for n in c[4:]]

x, y, z = at(3, 0)                     # a shell turned at one edge
q = [node(x, y, z), node(x + D, y, z), node(x + D, y + D, z), node(x, y + D, z)]
elem("S4", q, "S4_PRESCRIBED_ROTATION")
held([q[0], q[3]], 1, 6)
step1 += ["*BOUNDARY", f"{q[1]}, 5, 5, 0.002", f"{q[2]}, 5, 5, 0.002"]

c, _ = cube(4, 0, "S5_HELD_TEMPERATURE")
held(c[:4])
nset("NTFIX", c[4:])
model += ["*BOUNDARY", "NTFIX, 11, 11, 350."]

c, _ = cube(5, 0, "S6_CYLINDRICAL_TRANSFORM")      # DOFs radial / hoop / axial about the tile's own axis
x, y, z = at(5, 0)
nset("NCYL", c)
model += ["*TRANSFORM, NSET=NCYL, TYPE=C", f"{x + D / 2}, {y + D / 2}, 0., {x + D / 2}, {y + D / 2}, 1."]
model += ["*BOUNDARY"] + [f"{n}, 2, 3" for n in c[:4]] + [f"{c[0]}, 1, 1"]
step1 += ["*CLOAD"] + [f"{n}, 1, 5." for n in c[4:]]

# ---- row 1: loads ------------------------------------------------------------------
c, e_force = cube(0, 1, "L1_FORCE")
held(c[:4])
n_force = c[6]
step1 += ["*CLOAD", f"{n_force}, 1, 10.", f"{c[7]}, 3, -5."]

x, y, z = at(1, 1)                     # a beam: a force and moments at its tip
b = [node(x, y + D / 2, z + i * D / 2) for i in range(3)]
elem("B31", b[:2], "L2_MOMENT")
elem("B31", b[1:], "L2_MOMENT")
held(b[:1], 1, 6)
step1 += ["*CLOAD", f"{b[2]}, 1, 2.", f"{b[2]}, 5, 40.", f"{b[2]}, 6, -20."]

c, e = cube(2, 1, "L3_PRESSURE")
held(c[:4])
step1 += ["*DLOAD", f"{e}, P2, 1.", f"{e}, P4, 0.5"]

c, e = cube(3, 1, "L4_SURFACE_PRESSURE")
held(c[:4])
model += ["*SURFACE, NAME=SPRESS", f"{e}, S2", f"{e}, S3"]
step1 += ["*DSLOAD", "SPRESS, P, 0.8"]

x, y, z = at(4, 1)                     # a shell loaded on one edge, in its plane
q = [node(x, y, z), node(x + D, y, z), node(x + D, y + D, z), node(x, y + D, z)]
e = elem("S4", q, "L5_SHELL_EDGE_LOAD")
held([q[0], q[3]], 1, 6)
step1 += ["*DLOAD", f"{e}, EDNOR2, 0.5"]

x, y, z = at(5, 1)                     # a plane stress element: pressure on an edge
q = [node(x, y, 0), node(x + D, y, 0), node(x + D, y + D, 0), node(x, y + D, 0)]
e = elem("CPS4", q, "L6_PLANE_EDGE_PRESSURE")
held([q[0], q[3]], 1, 2)
step1 += ["*DLOAD", f"{e}, P2, 0.5"]

c, _ = cube(6, 1, "L7_GRAVITY")
held(c[:4])
step1 += ["*DLOAD", "L7_GRAVITY, GRAV, 9810., 0., 0., -1."]

c, _ = cube(7, 1, "L8_CENTRIFUGAL")
held(c[:4])
x, y, z = at(7, 1)
step1 += ["*DLOAD", f"L8_CENTRIFUGAL, CENTRIF, 1.e4, {x + D / 2}, {y + D / 2}, 0., 0., 0., 1."]

c, _ = cube(8, 1, "L9_GRAVITY_SIDEWAYS")
held(c[:4])
step1 += ["*DLOAD", "L9_GRAVITY_SIDEWAYS, GRAV, 4000., 1., 0., 0."]

if "bolt" not in OMIT:
    for k, col in enumerate((9, 10)):      # two bolts: cut between two elements, held at both ends
        lo, e_lo = cube(col, 1, f"L1{k}_BOLT", 0, D / 2)
        x, y, z = at(col, 1)
        top = [node(x, y, D), node(x + D, y, D), node(x + D, y + D, D), node(x, y + D, D)]
        elem("C3D8", lo[4:] + top, f"L1{k}_BOLT")
        held(lo[:4])
        held(top)
        ref = node(x + D / 2, y + D / 2, D + 6)
        model += [f"*SURFACE, NAME=SCUT{k}", f"{e_lo}, S2",
                  f"*PRE-TENSION SECTION, SURFACE=SCUT{k}, NODE={ref}", "0., 0., 1."]
        step1 += (["*CLOAD", f"{ref}, 1, {50. * (k + 1)}"])
        if k == 0: step2 += ["*BOUNDARY, FIXED", f"{ref}, 1, 1"]   # the first bolt: its preload held in the last step

if "bolt" not in OMIT:                 # a bolt buried in a block: the section inside the solid, the symbol drawn in front
    x, y, z = at(11, 1)
    g = {}                             # a 4 x 4 x 3 node grid, cubes sharing nodes; the centre column is the bolt
    for i in range(4):
        for j in range(4):
            for k in range(3):
                g[i, j, k] = node(x + i * D / 3, y + j * D / 3, z + k * D / 2)
    e_cut = None
    for i in range(3):
        for j in range(3):
            for k in range(2):
                c = [g[i, j, k], g[i + 1, j, k], g[i + 1, j + 1, k], g[i, j + 1, k],
                     g[i, j, k + 1], g[i + 1, j, k + 1], g[i + 1, j + 1, k + 1], g[i, j + 1, k + 1]]
                e = elem("C3D8", c, "L12_BOLT_IN_BLOCK")
                if i == 1 and j == 1 and k == 0: e_cut = e
    held([g[i, j, 0] for i in range(4) for j in range(4)])
    ref = node(x + D / 2, y + D / 2, z + D + 6)
    model += ["*SURFACE, NAME=SCUT2", f"{e_cut}, S2", f"*PRE-TENSION SECTION, SURFACE=SCUT2, NODE={ref}", "0., 0., 1."]
    step1 += ["*CLOAD", f"{ref}, 1, 80."]

# ---- row 2: thermal loads (step 1) ---------------------------------------------------
c, _ = cube(0, 2, "T1_NODE_HEAT")
held(c[:4])
step3 += ["*CFLUX", f"{c[6]}, 11, 0.5", f"{c[4]}, 11, -0.25"]

c, e = cube(1, 2, "T2_SURFACE_HEAT")
held(c[:4])
step3 += ["*DFLUX", f"{e}, S2, 0.02", f"{e}, S4, -0.01"]

c, e = cube(2, 2, "T3_FILM")
held(c[:4])
step3 += ["*FILM", f"{e}, F2, 293., 0.001", f"{e}, F4, 293., 0.0005"]

c, e = cube(3, 2, "T4_RADIATION")
held(c[:4])
step3 += ["*RADIATE", f"{e}, R2, 293., 0.8"]

c, _ = cube(4, 2, "T5_GIVEN_TEMPERATURE")           # heated in the static steps: it expands
held(c[:4])
nset("NHOT", c[4:])
step1 += ["*TEMPERATURE", "NHOT, 393."]

c, _ = cube(5, 2, "T6_BODY_HEAT")
held(c[:4])
step3 += ["*DFLUX", "T6_BODY_HEAT, BF, 0.001"]

# ---- row 3: constraints and discrete elements ------------------------------------------
c, _ = cube(0, 3, "C1_RIGID_BODY")
held(c[:4])
x, y, z = at(0, 3)
ref = node(x + D / 2, y + D / 2, D + 5)
nset("NRIGID", c[4:])
model += [f"*RIGID BODY, NSET=NRIGID, REF NODE={ref}"]
step1 += ["*CLOAD", f"{ref}, 1, 5."]

c, e = cube(1, 3, "C2_KINEMATIC_COUPLING")
held(c[:4])
x, y, z = at(1, 3)
ref = node(x + D / 2, y + D / 2, D + 5)
model += ["*SURFACE, NAME=SKIN", f"{e}, S2", f"*COUPLING, REF NODE={ref}, SURFACE=SKIN, CONSTRAINT NAME=KIN",
          "*KINEMATIC", "1, 3"]
step1 += ["*CLOAD", f"{ref}, 2, 5."]

c, e = cube(2, 3, "C3_DISTRIBUTING_COUPLING")
held(c[:4])
x, y, z = at(2, 3)
ref = node(x + D / 2, y + D / 2, D + 5)
model += ["*SURFACE, NAME=SDIS", f"{e}, S2", f"*COUPLING, REF NODE={ref}, SURFACE=SDIS, CONSTRAINT NAME=DIS",
          "*DISTRIBUTING", "1, 3"]
step1 += ["*CLOAD", f"{ref}, 3, -5."]

c, _ = cube(8, 3, "C12_RIGID_BODY_ROT")              # a rigid top turned by a moment on its ROT NODE
held(c[:4])
x, y, z = at(8, 3)
ref = node(x + D / 2, y + D / 2, D + 5)
rot = node(x + D / 2, y + D / 2, D + 5)
nset("NRIGID2", c[4:])
model += [f"*RIGID BODY, NSET=NRIGID2, REF NODE={ref}, ROT NODE={rot}"]
step1 += ["*CLOAD", f"{rot}, 3, 30."]

a, _ = cube(3, 3, "C4_EQUATION")                    # two tops tied in z by an equation
held(a[:4])
b, _ = cube(4, 3, "C5_MPC_BEAM")
held(b[:4])
model += ["*EQUATION", "2", f"{a[5]}, 3, 1., {b[4]}, 3, -1."]
step1 += ["*CLOAD", f"{a[5]}, 3, 5."]
if "mpc" not in OMIT:
    model += ["*MPC", f"BEAM, {a[6]}, {b[7]}"]             # and kept at their distance by a beam MPC

lo, e_lo = cube(5, 3, "C6_TIE", 0, D / 2)           # two blocks, separate nodes, tied
x, y, z = at(5, 3)
up, e_up = cube(5, 3, "C6_TIE", D / 2, D / 2)
held(lo[:4])
model += ["*SURFACE, NAME=TIE_M", f"{e_lo}, S2", "*SURFACE, NAME=TIE_S", f"{e_up}, S1",
          "*TIE, NAME=TIE1, POSITION TOLERANCE=0.1", "TIE_S, TIE_M"]
step1 += ["*CLOAD", f"{up[6]}, 1, 5."]

if "contact" not in OMIT:
    lo, e_lo = cube(6, 3, "C7_CONTACT", 0, D / 2)       # two blocks pressed together
    up, e_up = cube(6, 3, "C7_CONTACT", D / 2 - 0.001, D / 2)
    held(lo[:4])
    model += ["*BOUNDARY"] + [f"{n}, 1, 2" for n in up]
    model += ["*SURFACE, NAME=CON_M", f"{e_lo}, S2", "*SURFACE, NAME=CON_S, TYPE=NODE"] + [str(n) for n in up[:4]]
    model += ["*CONTACT PAIR, INTERACTION=TOUCH, TYPE=NODE TO SURFACE", "CON_S, CON_M",
              "*SURFACE INTERACTION, NAME=TOUCH", "*SURFACE BEHAVIOR, PRESSURE-OVERCLOSURE=LINEAR", "1.e4"]
    step1 += ["*DLOAD", f"{e_up}, P2, 0.2"]

if "discrete" not in OMIT:
    x, y, z = at(7, 3)                                  # discrete elements on a held base
    base = [node(x + i * 3.0, y, 0) for i in range(4)]
    tip = [node(x + i * 3.0, y, D) for i in range(4)]
    held(base, 1, 3)
    model += ["*BOUNDARY"] + [f"{n}, 1, 2" for n in tip]
    # nodes that only carry springs conduct no heat: the heat step needs their temperature given
    model += ["*BOUNDARY"] + [f"{n}, 11, 11, 293." for n in base + tip]
    e_sp = elem("SPRINGA", [base[0], tip[0]], "C8_SPRING")
    e_da = elem("DASHPOTA", [base[1], tip[1]], "C9_DASHPOT")
    e_sp2 = elem("SPRINGA", [base[1], tip[1]], "C8_SPRING")       # the dashpot needs a stiffness beside it
    e_ga = elem("GAPUNI", [base[2], tip[2]], "C10_GAP")
    e_sp3 = elem("SPRINGA", [base[2], tip[2]], "C8_SPRING")
    e_sp4 = elem("SPRINGA", [base[3], tip[3]], "C8_SPRING")
    elem("MASS", [tip[3]], "C11_MASS")
    model += ["*SPRING, ELSET=C8_SPRING", "", "100.", "*DASHPOT, ELSET=C9_DASHPOT", "", "1.",
              "*GAP, ELSET=C10_GAP", "5., 0., 0., 1.", "*MASS, ELSET=C11_MASS", "1.e-6"]
    step1 += ["*CLOAD"] + [f"{n}, 3, 1." for n in tip]

# ---- the last step: every point load goes (OP=NEW) but the first load tile's force, which
# turns round and doubles; a pressure comes on its top ---------------------------------
step2 += ["*CLOAD, OP=NEW", f"{n_force}, 1, -20.", "*DLOAD", f"{e_force}, P2, 1."]

# ---- write -----------------------------------------------------------------------------
solid = sorted({s for _, t, _, s in elems if t in ("C3D8",)})
shell = sorted({s for _, t, _, s in elems if t == "S4"})
out = ["*HEADING", "ccxview symbols: every support, load, thermal load and constraint, one tile each",
       "*NODE, NSET=NALL"]
out += [f"{i}, {x:g}, {y:g}, {z:g}" for i, (x, y, z) in nodes.items()]
by = {}
for i, t, c, s in elems:
    by.setdefault((t, s), []).append((i, c))
for (t, s), es in by.items():
    out.append(f"*ELEMENT, TYPE={t}, ELSET={s}")
    out += [", ".join(map(str, [i] + c)) for i, c in es]
for name, ids in nsets.items():
    out.append(f"*NSET, NSET={name}")
    out += [", ".join(map(str, ids[i:i + 12])) for i in range(0, len(ids), 12)]
out += ["*MATERIAL, NAME=STEEL", "*ELASTIC", "210000., 0.3", "*DENSITY", "7.85e-9", "*EXPANSION", "1.2e-5",
        "*CONDUCTIVITY", "50.", "*SPECIFIC HEAT", "4.6e8"]
out += [f"*SOLID SECTION, ELSET={s}, MATERIAL=STEEL" for s in solid]
out += [line for s in shell for line in (f"*SHELL SECTION, ELSET={s}, MATERIAL=STEEL", "1.")]
out += ["*SOLID SECTION, ELSET=L6_PLANE_EDGE_PRESSURE, MATERIAL=STEEL", "1.",
        "*BEAM SECTION, ELSET=L2_MOMENT, MATERIAL=STEEL, SECTION=RECT", "2., 2.", "1., 0., 0."]
out += ["*INITIAL CONDITIONS, TYPE=TEMPERATURE", "NALL, 293.", "*PHYSICAL CONSTANTS, ABSOLUTE ZERO=0., STEFAN BOLTZMANN=5.669e-11"]
out += model
req = ["*NODE FILE", "U, NT, RF", "*EL FILE", "S, E, HFL"]
out += ["*STEP", "*HEAT TRANSFER", "1., 1."] + step3 + req + ["*END STEP"]
out += ["*STEP", "*STATIC"] + step1 + req + ["*END STEP"]
out += ["*STEP", "*STATIC"] + step2 + req + ["*END STEP"]

path = os.environ.get("SHOWCASE_OUT") or os.path.join(os.path.dirname(__file__), "..", "samples", "symbols", "symbols.inp")
with open(path, "w") as f:
    f.write("\n".join(out) + "\n")
print(f"{path}: {len(nodes)} nodes, {len(elems)} elements")
