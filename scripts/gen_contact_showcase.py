#!/usr/bin/env python3
"""gen_contact_showcase.py -- write samples/contact_showcase/contact_showcase.inp:
five small, separate contact models in one deck, each showing one behaviour
develop over the 8 increments of one static step, to see what ccxview's contact
display shows (the Contact window, --opt contact_mode=links,status,gap,solids).
Solved by ccx: scripts/solve_showcase.sh contact_showcase

mm, N, MPa, steel, linear, no NLGEOM.  Each model has its own parts, contact
pair (or tie), supports and loads; the sets are named after the model
(A_GAP_BLOCK, A_GAP_PLATE, A_GAP_SLAVE, A_GAP_MASTER, ...). They stand on a
grid: A B C 75 mm apart along x, D and E 60 mm in front of them.

A  gap closing     a block standing tilted on a plate (its bottom rising 0.4 over
                   20 mm), its top pushed down 0.3: the contact spreads from the
                   low edge (open -> near -> closed).
B  sliding         a block pressed onto a plate (increments 1-2), then its top
                   pushed along x (3-8), friction 0.3: the nodes stick first,
                   then slide, from the leading edge back, until the whole face
                   slides (CSLIP grows, status sticking -> sliding).
C  lift-off        a block pressed onto a plate (1-2) through a rigid top, then
                   the top turned about y (3-8): one side lifts off.
D  interface       a laminate: a thin top layer on a bottom layer, held down by
   opening         a pressure, the end of the top layer pulled up (2-8). The
                   interface is a frictionless no-tension contact: it opens from
                   the end, the opening front running along it. CalculiX 2.22
                   has no cohesive contact, so this is not delamination with a
                   strength, only the opening of an interface that takes no
                   tension.
E  tie             a block tied onto a base it overhangs, the meshes not matching;
                   ccx writes the slave nodes past the base's end, which it
                   cannot tie, to contact_showcase_WarnNodeMissTiedContact.nam.

A surface to surface pair (a punch on a block) was tried as model F: ccx 2.22
stops with "element slave surface ... does not exist" as soon as one deck mixes
node to surface and surface to surface pairs, so it is left out
(samples/contact/contact_s2s shows surface to surface).

Node to surface for A-D (slave meshes finer than the master's and offset from
it: ccx stalls on slave nodes sitting on master nodes), LINEAR
pressure-overclosure with c0 so that open slave nodes near the master get
contact elements too. *NODE FILE, CONTACT ELEMENTS makes ccx write the .cel;
*CONTACT FILE CDIS, CSTR the CONTACT block (COPEN, CSLIP, CPRESS, CSHEAR).
SHOWCASE_OMIT=B,D leaves those models' contact pairs (E: the tie) out, to find
the one a run that does not converge stalls on.
"""
import math
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples", "contact_showcase")
NAME = "contact_showcase"
INC = 0.125                     # 8 increments

nodes = {}
elsets = []                     # (name, [(eid, (i,j,k), conn)])
nsets = {}
surfs = {}
eid_next = [1]


def wrap(vals, per=16):
    vals = list(vals)
    return [", ".join(map(str, vals[i:i + per])) + ("," if i + per < len(vals) else "")
            for i in range(0, len(vals), per)]


def grid(name, nx, ny, nz, fx):
    """a structured C3D8I block as the element set NAME; {(i,j,k): node}, [(eid, ijk)]"""
    idx = {}
    for k in range(nz + 1):
        for j in range(ny + 1):
            for i in range(nx + 1):
                n = len(nodes) + 1
                idx[(i, j, k)] = n
                nodes[n] = fx(i / nx, j / ny, k / nz)
    el = []
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                c = [idx[(i, j, k)], idx[(i + 1, j, k)], idx[(i + 1, j + 1, k)], idx[(i, j + 1, k)],
                     idx[(i, j, k + 1)], idx[(i + 1, j, k + 1)], idx[(i + 1, j + 1, k + 1)], idx[(i, j + 1, k + 1)]]
                el.append((eid_next[0], (i, j, k), c))
                eid_next[0] += 1
    elsets.append((name, el))
    return idx, el


def box(name, x0, y0, z0, lx, ly, lz, nx, ny, nz):
    return grid(name, nx, ny, nz, lambda u, v, w: (x0 + lx * u, y0 + ly * v, z0 + lz * w))


def face(el, k):
    """hex faces: S1 = nodes 1-2-3-4 (k = 0 side), S2 = 5-6-7-8"""
    return [(e, "S1" if k == 0 else "S2") for e, g, c in el if g[2] == (0 if k == 0 else max(h[2] for _, h, _ in el))]


def nodes_where(idx, f):
    return [n for g, n in idx.items() if f(*g)]


def extra_node(x, y, z):
    n = len(nodes) + 1
    nodes[n] = (x, y, z)
    return n


# the grid: slot origins (x0, y0)
SLOT = {"A": (0, 0), "B": (75, 0), "C": (150, 0), "D": (30, -60), "E": (125, -60)}
model_bc, step_bc, step_amp_bc, step_load, amps = [], [], [], [], []
interactions = []

amps += ["*AMPLITUDE, NAME=PRESS", "0., 0., 0.25, 1., 1., 1.",
         "*AMPLITUDE, NAME=MOVE", "0., 0., 0.25, 0., 1., 1.",
         "*AMPLITUDE, NAME=HOLD", "0., 0., 0.125, 1., 1., 1.",
         "*AMPLITUDE, NAME=PEEL", "0., 0., 0.125, 0., 1., 1."]

# A: a tilted block on a plate, its top pushed down
x0, y0 = SLOT["A"]
pidx, pel = box("A_GAP_PLATE", x0 - 15, y0 - 10, -6, 30, 20, 6, 9, 6, 1)
RISE, PUSH_A = 0.4, 0.3
a = math.atan2(RISE, 20.0)
ca, sa = math.cos(a), math.sin(a)
bidx, bel = grid("A_GAP_BLOCK", 10, 6, 3, lambda u, v, w: (x0 - 10 + 20 * u * ca - 10 * w * sa,
                                                         y0 - 6 + 12 * v, 20 * u * sa + 10 * w * ca))
nsets["A_GAP_FIX"] = nodes_where(pidx, lambda i, j, k: k == 0)
nsets["A_GAP_TOP"] = nodes_where(bidx, lambda i, j, k: k == 3)
surfs["A_GAP_MASTER"] = face(pel, 1)
surfs["A_GAP_SLAVE"] = face(bel, 0)
model_bc += ["A_GAP_FIX, 1, 3", "A_GAP_TOP, 1, 2"]
step_bc += ["A_GAP_TOP, 3, 3, %g" % -PUSH_A]
interactions.append(("A_GAP", "A_GAP_SLAVE", "A_GAP_MASTER", "NODE TO SURFACE", "1000000., 0., 1.", None))

# B: a block pressed, then pushed along x, with friction
x0, y0 = SLOT["B"]
pidx, pel = box("B_SLIDE_PLATE", x0 - 15, y0 - 10, -6, 30, 20, 6, 9, 6, 1)
bidx, bel = box("B_SLIDE_BLOCK", x0 - 8.2, y0 - 6.1, 0, 16, 12, 8, 8, 6, 3)
nsets["B_SLIDE_FIX"] = nodes_where(pidx, lambda i, j, k: k == 0)
nsets["B_SLIDE_TOP"] = nodes_where(bidx, lambda i, j, k: k == 3)
surfs["B_SLIDE_MASTER"] = face(pel, 1)
surfs["B_SLIDE_SLAVE"] = face(bel, 0)
model_bc += ["B_SLIDE_FIX, 1, 3", "B_SLIDE_TOP, 2, 2"]
step_amp_bc += [("PRESS", "B_SLIDE_TOP, 3, 3, -0.02"), ("MOVE", "B_SLIDE_TOP, 1, 1, 0.04")]
interactions.append(("B_SLIDE", "B_SLIDE_SLAVE", "B_SLIDE_MASTER", "NODE TO SURFACE", "1000000., 0., 1.",
                     "0.3, 50000."))

# C: a block pressed through a rigid top, then the top turned about y
x0, y0 = SLOT["C"]
pidx, pel = box("C_LIFT_PLATE", x0 - 15, y0 - 10, -6, 30, 20, 6, 9, 6, 1)
bidx, bel = box("C_LIFT_BLOCK", x0 - 10.2, y0 - 6.1, 0, 20, 12, 8, 10, 6, 3)
nsets["C_LIFT_FIX"] = nodes_where(pidx, lambda i, j, k: k == 0)
nsets["C_LIFT_TOP"] = nodes_where(bidx, lambda i, j, k: k == 3)
ref = extra_node(x0 - 0.2, y0 - 0.1, 8)
rot = extra_node(x0 - 0.2, y0 - 0.1, 12)
nsets["C_LIFT_REF"] = [ref]
nsets["C_LIFT_ROT"] = [rot]
surfs["C_LIFT_MASTER"] = face(pel, 1)
surfs["C_LIFT_SLAVE"] = face(bel, 0)
model_bc += ["C_LIFT_FIX, 1, 3", "C_LIFT_REF, 1, 2", "C_LIFT_ROT, 1, 1", "C_LIFT_ROT, 3, 3"]
step_amp_bc += [("PRESS", "C_LIFT_REF, 3, 3, -0.01"), ("MOVE", "C_LIFT_ROT, 2, 2, 0.005")]
interactions.append(("C_LIFT", "C_LIFT_SLAVE", "C_LIFT_MASTER", "NODE TO SURFACE", "1000000., 0., 1.", None))
rigid = "*RIGID BODY, NSET=C_LIFT_TOP, REF NODE=%d, ROT NODE=%d" % (ref, rot)

# D: a laminate, the top layer held down by a pressure and peeled at its end
x0, y0 = SLOT["D"]
LD = 60.0
didx, delb = box("D_LAM_BOTTOM", x0 - LD / 2, y0 - 5, -4, LD, 10, 4, 15, 2, 1)
tidx, tel = box("D_LAM_TOP", x0 - LD / 2 + 0.5, y0 - 4.8, 0, LD - 1, 9.6, 2, 24, 3, 1)
nsets["D_LAM_FIX"] = nodes_where(didx, lambda i, j, k: k == 0)
nsets["D_LAM_HELD"] = nodes_where(tidx, lambda i, j, k: i == 0)
nsets["D_LAM_END"] = nodes_where(tidx, lambda i, j, k: i == 24)
surfs["D_LAM_MASTER"] = face(delb, 1)
surfs["D_LAM_SLAVE"] = face(tel, 0)
model_bc += ["D_LAM_FIX, 1, 3", "D_LAM_HELD, 1, 2"]
step_amp_bc += [("PEEL", "D_LAM_END, 3, 3, 0.6")]
d_press = [f"{e}, P2, 0.05" for e, s in face(tel, 1)]
interactions.append(("D_LAM", "D_LAM_SLAVE", "D_LAM_MASTER", "NODE TO SURFACE", "1000000., 0., 1.", None))

# E: a block tied onto a base it overhangs
x0, y0 = SLOT["E"]
eidx, eel = box("E_TIE_BASE", x0 - 18, y0 - 8, -8, 30, 16, 8, 6, 4, 2)
uidx, uel = box("E_TIE_BLOCK", x0 - 6, y0 - 8, 0, 24, 16, 8, 9, 5, 2)
nsets["E_TIE_FIX"] = nodes_where(eidx, lambda i, j, k: i == 0)
nsets["E_TIE_END"] = nodes_where(uidx, lambda i, j, k: i == 9 and k == 2)
surfs["E_TIE_MASTER"] = face(eel, 1)
surfs["E_TIE_SLAVE"] = face(uel, 0)
model_bc += ["E_TIE_FIX, 1, 3"]
step_load += [f"{n}, 3, -20." for n in nsets["E_TIE_END"]]

omit = set(os.environ.get("SHOWCASE_OMIT", "").upper().split(",")) - {""}

L = ["*HEADING", "ccxview sample: five contact models, A gap B slide C lift-off D interface opening E tie",
     "*NODE, NSET=NALL"]
L += [f"{n}, {x:.8g}, {y:.8g}, {z:.8g}" for n, (x, y, z) in nodes.items()]
for name, el in elsets:
    L.append(f"*ELEMENT, TYPE=C3D8I, ELSET={name}")
    L += [", ".join(map(str, [e] + c)) for e, g, c in el]
for name, ns in nsets.items():
    L += [f"*NSET, NSET={name}"] + wrap(ns)
for name, fs in surfs.items():
    L += [f"*SURFACE, NAME={name}"] + [f"{e}, {s}" for e, s in fs]
L += ["*MATERIAL, NAME=STEEL", "*ELASTIC", "210000., 0.3"]
L += [f"*SOLID SECTION, ELSET={name}, MATERIAL=STEEL" for name, el in elsets]
L.append(rigid)
for name, slave, master, kind, beh, fric in interactions:
    if name[0] in omit:
        continue
    L += [f"*SURFACE INTERACTION, NAME={name}", "*SURFACE BEHAVIOR, PRESSURE-OVERCLOSURE=LINEAR", beh]
    if fric:
        L += ["*FRICTION", fric]
    L += [f"*CONTACT PAIR, INTERACTION={name}, TYPE={kind}", f"{slave}, {master}"]
if "E" not in omit:
    L += ["*TIE, NAME=E_TIE, POSITION TOLERANCE=0.5", "E_TIE_SLAVE, E_TIE_MASTER"]
L += amps
L += ["*BOUNDARY"] + model_bc
L += ["*STEP, INC=200", "*STATIC, DIRECT", f"{INC:g}, 1.", "*BOUNDARY"] + step_bc
for amp, line in step_amp_bc:
    L += [f"*BOUNDARY, AMPLITUDE={amp}", line]
L += ["*CLOAD"] + step_load
L += ["*DLOAD, AMPLITUDE=HOLD"] + d_press
L += ["*NODE FILE, CONTACT ELEMENTS", "U",
      "*EL FILE", "S",
      "*CONTACT FILE", "CDIS, CSTR",
      "*END STEP"]
os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, NAME + ".inp"), "w") as f:
    f.write("\n".join(L) + "\n")
print(f"{NAME}.inp: {len(nodes)} nodes, {eid_next[0] - 1} elements")
