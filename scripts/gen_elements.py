#!/usr/bin/env python3
"""gen_elements.py -- write samples/elements/elements.inp: one small CalculiX deck
that holds every element type and modelling feature ccxview draws, so a single
file exercises the whole viewer.  Run ccx on it (scripts/solve_showcase.sh elements) to
get the matching .frd / .dat.

Parts sit side by side along X (y, z within 0..20):
  x   0..10   C3D8 block, pressure on top, far face a *RIGID BODY with a loaded ref node
  x  20..30   C3D20R block, far face a kinematic *COUPLING, load at its ref node
  x  40..44   C3D4 tet          x 50..54  C3D10 tet, apex tied to the C3D4 apex by *EQUATION
  x  60..64   C3D6 wedge        x 70..74  C3D15 wedge, top nodes a *DISTRIBUTING COUPLING
  x  80..90   S4 shell patch    x 95..105 S8 patch    x 110  S3    x 120  S6
  x 130..140  B31 cantilever    x 145..155 B32         x 160..170 T3D2 truss
  x 180..     SPRINGA + DASHPOTA (side by side), grounded SPRING1, MASS, GAPUNI
  x 200..210  two C3D8 blocks joined by *TIE
  x 220..230  two C3D8 blocks in *CONTACT PAIR (small initial gap, pressed shut)
Step 1: *STATIC with every load (node-to-surface contact; surface-to-surface and
NLGEOM make ccx 2.22 diverge on the expanded beams).  Step 2: *FREQUENCY, 8 modes.
"""
import os
import sys

OMIT = set(os.environ.get("SHOWCASE_OMIT", "").split(","))    # debugging: parts to leave out
OUT = os.environ.get("SHOWCASE_OUT")                          # output path override


def want(part):
    return part not in OMIT

nodes = {}          # id -> (x, y, z)
elems = []          # (id, type, [nodes], elset)
nsets, elsets = {}, {}
lines = []          # deck body after mesh
nid = [0]
eid = [0]


def wrap(vals, per=16):
    """CalculiX takes at most 16 entries per line"""
    vals = list(vals)
    return [", ".join(map(str, vals[i:i + per])) + ("," if i + per < len(vals) else "")
            for i in range(0, len(vals), per)]


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


# ---- structured hex blocks (CalculiX node order) -----------------------------

def hex8_block(x0, y0, z0, nx, ny, nz, h, elset):
    """returns dict (i,j,k) -> node id"""
    g = {}
    for k in range(nz + 1):
        for j in range(ny + 1):
            for i in range(nx + 1):
                g[i, j, k] = node(x0 + i * h, y0 + j * h, z0 + k * h)
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                c = [g[i, j, k], g[i + 1, j, k], g[i + 1, j + 1, k], g[i, j + 1, k],
                     g[i, j, k + 1], g[i + 1, j, k + 1], g[i + 1, j + 1, k + 1], g[i, j + 1, k + 1]]
                elem("C3D8", c, elset)
    return g


def hex20_block(x0, y0, z0, nx, ny, nz, h, elset):
    """grid on doubled indices; a point exists when at most one index is odd"""
    g = {}

    def P(a, b, c):
        if (a, b, c) not in g:
            g[a, b, c] = node(x0 + a * h / 2, y0 + b * h / 2, z0 + c * h / 2)
        return g[a, b, c]

    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                a, b, c = 2 * i, 2 * j, 2 * k
                corners = [(a, b, c), (a + 2, b, c), (a + 2, b + 2, c), (a, b + 2, c),
                           (a, b, c + 2), (a + 2, b, c + 2), (a + 2, b + 2, c + 2), (a, b + 2, c + 2)]
                mids = [(a + 1, b, c), (a + 2, b + 1, c), (a + 1, b + 2, c), (a, b + 1, c),          # bottom 9-12
                        (a + 1, b, c + 2), (a + 2, b + 1, c + 2), (a + 1, b + 2, c + 2), (a, b + 1, c + 2),  # top 13-16
                        (a, b, c + 1), (a + 2, b, c + 1), (a + 2, b + 2, c + 1), (a, b + 2, c + 1)]  # vertical 17-20
                elem("C3D20R", [P(*p) for p in corners + mids], elset)
    return g


def mid(a, b):
    pa, pb = nodes[a], nodes[b]
    return node(*[(u + v) / 2 for u, v in zip(pa, pb)])


# ---- 1. C3D8 block, rigid far face ---------------------------------------------
g = hex8_block(0, 0, 0, 2, 2, 2, 5, "EHEX8")
nset("NFIX_HEX8", [g[0, j, k] for j in range(3) for k in range(3)])
nset("NRIGID", [g[2, j, k] for j in range(3) for k in range(3)])
elsets["EHEX8_TOP"] = [e[0] for e in elems if e[3] == "EHEX8" and nodes[e[2][4]][2] == 10.0]
R1 = node(14, 5, 5)
R1rot = node(14, 5, 6)
nset("NREF1", [R1])

# ---- 2. C3D20R block, kinematic coupling on the far face -----------------------
g20 = hex20_block(20, 0, 0, 1, 1, 2, 10, "EHEX20")
nset("NFIX_HEX20", [n for (a, b, c), n in g20.items() if a == 0])
elsets["EHEX20_FAR"] = list(elsets["EHEX20"])
R2 = node(34, 5, 10)
nset("NREF2", [R2])

# ---- 3. tets --------------------------------------------------------------------
t = [node(40, 0, 0), node(44, 0, 0), node(40, 4, 0), node(40, 0, 6)]
elem("C3D4", t, "ETET4")
nset("NFIX_TET4", t[:3])
apex4 = t[3]
t = [node(50, 0, 0), node(54, 0, 0), node(50, 4, 0), node(50, 0, 6)]
t10 = t + [mid(t[0], t[1]), mid(t[1], t[2]), mid(t[2], t[0]), mid(t[0], t[3]), mid(t[1], t[3]), mid(t[2], t[3])]
elem("C3D10", t10, "ETET10")
nset("NFIX_TET10", t[:3] + t10[4:7])
apex10 = t[3]

# ---- 4. wedges ------------------------------------------------------------------
w = [node(60, 0, 0), node(64, 0, 0), node(60, 4, 0), node(60, 0, 6), node(64, 0, 6), node(60, 4, 6)]
elem("C3D6", w, "EWEDGE6")
nset("NFIX_WEDGE6", w[:3])
nset("NLOAD_WEDGE6", w[3:])
w = [node(70, 0, 0), node(74, 0, 0), node(70, 4, 0), node(70, 0, 6), node(74, 0, 6), node(70, 4, 6)]
w15 = w + [mid(w[0], w[1]), mid(w[1], w[2]), mid(w[2], w[0]),
           mid(w[3], w[4]), mid(w[4], w[5]), mid(w[5], w[3]),
           mid(w[0], w[3]), mid(w[1], w[4]), mid(w[2], w[5])]
elem("C3D15", w15, "EWEDGE15")
nset("NFIX_WEDGE15", w[:3] + w15[6:9])
R3 = node(72, 1.3, 9)
nset("NREF3", [R3])
if want("dcoup"):
    elem("DCOUP3D", [R3], "EDC")
dc_nodes = w[3:] + w15[9:12]

# ---- 5. shells --------------------------------------------------------------------
def quad_patch(x0, y0, n, h, elset, quadratic):
    g = {}
    for j in range(n + 1):
        for i in range(n + 1):
            g[i, j] = node(x0 + i * h, y0 + j * h, 0)
    m = {}

    def M(a, b):
        if (a, b) not in m and (b, a) not in m:
            m[a, b] = mid(a, b)
        return m.get((a, b), m.get((b, a)))

    for j in range(n):
        for i in range(n):
            c = [g[i, j], g[i + 1, j], g[i + 1, j + 1], g[i, j + 1]]
            if quadratic:
                c += [M(c[0], c[1]), M(c[1], c[2]), M(c[2], c[3]), M(c[3], c[0])]
            elem("S8" if quadratic else "S4", c, elset)
    return g


gs = quad_patch(80, 0, 2, 5, "ES4", False)
nset("NFIX_S4", [gs[0, j] for j in range(3)])
gs = quad_patch(95, 0, 2, 5, "ES8", True)
nset("NFIX_S8", [gs[0, j] for j in range(3)])
s = [node(110, 0, 0), node(116, 0, 0), node(110, 6, 0)]
elem("S3", s, "ES3")
nset("NFIX_S3", s[:2])
s = [node(120, 0, 0), node(126, 0, 0), node(120, 6, 0)]
elem("S6", s + [mid(s[0], s[1]), mid(s[1], s[2]), mid(s[2], s[0])], "ES6")
nset("NFIX_S6", s[:2])

# ---- 6. beams and truss ----------------------------------------------------------
if want("beam"):
    b = [node(130 + 2.5 * i, 0, 0) for i in range(5)]
    for i in range(4):
        elem("B31", [b[i], b[i + 1]], "EB31")
    nset("NFIX_B31", [b[0]])
    nset("NTIP_B31", [b[4]])
    b = [node(145 + 2.5 * i, 0, 0) for i in range(5)]
    for i in range(0, 4, 2):
        elem("B32", [b[i], b[i + 1], b[i + 2]], "EB32")    # end, middle, end
    nset("NFIX_B32", [b[0]])
    nset("NTIP_B32", [b[4]])
if want("truss"):
    ta, tb, tc = node(160, 0, 0), node(170, 0, 0), node(165, 0, 6)
    elem("T3D2", [ta, tc], "ET3D2")
    elem("T3D2", [tb, tc], "ET3D2")
    nset("NFIX_TRUSS", [ta, tb])
    nset("NTIP_TRUSS", [tc])

# ---- 7. discrete elements ----------------------------------------------------------
sa, sb = node(180, 0, 0), node(186, 0, 0)
elem("SPRINGA", [sa, sb], "ESPRINGA")
elem("DASHPOTA", [sa, sb], "EDASHPOT")
elem("MASS", [sb], "EMASS")
nset("NSPRING_FIX", [sa])
nset("NSPRING_LOAD", [sb])
sc = node(180, 6, 0)
elem("SPRING1", [sc], "ESPRING1")
nset("NSPRING1", [sc])
if want("gap"):
    gd, ge = node(180, 12, 0), node(184, 12, 0)
    elem("GAPUNI", [gd, ge], "EGAP")
    elem("SPRING1", [gd], "ESPRING1")
    nset("NGAP_LOAD", [gd])
    nset("NGAP_FIX", [ge])

# ---- 8. tie -----------------------------------------------------------------------
gl = hex8_block(200, 0, 0, 2, 2, 1, 5, "ETIE_LO")
gu = hex8_block(200, 0, 5, 2, 2, 1, 5, "ETIE_UP")
nset("NFIX_TIE", [gl[i, j, 0] for i in range(3) for j in range(3)])

# ---- 9. contact -------------------------------------------------------------------
if want("contact"):
    cl = hex8_block(220, 0, 0, 2, 2, 1, 5, "ECON_LO")
    cu = hex8_block(220, 0, 5.0, 2, 2, 1, 5, "ECON_UP")
    nset("NFIX_CON", [cl[i, j, 0] for i in range(3) for j in range(3)])
    nset("NCON_UP_SIDE", [cu[0, j, k] for j in range(3) for k in range(2)])
    # until the contact closes the upper block would float in z: soft grounded springs hold it
    for i in (0, 2):
        for j in (0, 2):
            elem("SPRING1", [cu[i, j, 1]], "ESOFT")

# ---- write --------------------------------------------------------------------------
out = ["*HEADING", "ccxview elements: every element type and modelling feature in one deck",
       "*NODE, NSET=NALL"]
for i, (x, y, z) in nodes.items():
    out.append(f"{i}, {x:g}, {y:g}, {z:g}")
cur = None
for i, t, c, es in elems:
    if (t, es) != cur:
        out.append(f"*ELEMENT, TYPE={t}, ELSET={es}")
        cur = (t, es)
    out += wrap([i] + c)
for name, ids in nsets.items():
    out.append(f"*NSET, NSET={name}")
    out += wrap(sorted(set(ids)))
for name in ("EHEX8_TOP", "EHEX20_FAR"):
    out.append(f"*ELSET, ELSET={name}")
    out += wrap(elsets[name])
out += [
    "*ELSET, ELSET=ESOLID", ", ".join(e for e in ("EHEX8", "EHEX20", "ETET4", "ETET10", "EWEDGE6", "EWEDGE15",
                                                 "ETIE_LO", "ETIE_UP", "ECON_LO", "ECON_UP") if e in elsets),
    "*SURFACE, NAME=SHEX20_FAR, TYPE=ELEMENT", "EHEX20_FAR, S4",
    "*SURFACE, NAME=STIE_LO, TYPE=ELEMENT", "ETIE_LO, S2",
    "*SURFACE, NAME=STIE_UP, TYPE=ELEMENT", "ETIE_UP, S1",
    "*SURFACE, NAME=SCON_LO, TYPE=ELEMENT", "ECON_LO, S2",
    "*SURFACE, NAME=SCON_UP, TYPE=ELEMENT", "ECON_UP, S1",
    "*MATERIAL, NAME=STEEL", "*ELASTIC", "210000., 0.3", "*DENSITY", "7.85e-9",
    "*MATERIAL, NAME=ALU", "*ELASTIC", "70000., 0.33", "*DENSITY", "2.7e-9",
    "*SOLID SECTION, ELSET=ESOLID, MATERIAL=STEEL",
    "*SOLID SECTION, ELSET=ET3D2, MATERIAL=ALU", "2.",
    "*SHELL SECTION, ELSET=ES4, MATERIAL=ALU", "1.",
    "*SHELL SECTION, ELSET=ES8, MATERIAL=ALU", "1.",
    "*SHELL SECTION, ELSET=ES3, MATERIAL=ALU", "1.",
    "*SHELL SECTION, ELSET=ES6, MATERIAL=ALU", "1.",
    "*BEAM SECTION, ELSET=EB31, MATERIAL=STEEL, SECTION=RECT", "1., 1.", "0., 0., 1.",
    "*BEAM SECTION, ELSET=EB32, MATERIAL=STEEL, SECTION=RECT", "1., 1.", "0., 0., 1.",
    "*SPRING, ELSET=ESPRINGA", "1000.",
    "*DASHPOT, ELSET=EDASHPOT", "", "0.1",
    "*MASS, ELSET=EMASS", "1e-3",
    "*SPRING, ELSET=ESPRING1", "1", "500.",
    "*SPRING, ELSET=ESOFT", "3", "20.",
    "*GAP, ELSET=EGAP", "0.5, 1., 0., 0.",
    f"*RIGID BODY, NSET=NRIGID, REF NODE={R1}, ROT NODE={R1rot}",
    f"*COUPLING, REF NODE={R2}, SURFACE=SHEX20_FAR, CONSTRAINT NAME=CPL1", "*KINEMATIC", "1, 3",
    "*DISTRIBUTING COUPLING, ELSET=EDC",
]
out += [f"{n}, 1." for n in dc_nodes]
out += [
    "*EQUATION", "2", f"{apex4}, 3, 1., {apex10}, 3, -1.",
    "*TIE, NAME=TIE1, POSITION TOLERANCE=0.5", "STIE_UP, STIE_LO",
    "*SURFACE INTERACTION, NAME=SI1", "*SURFACE BEHAVIOR, PRESSURE-OVERCLOSURE=LINEAR", "1e5",
    "*CONTACT PAIR, INTERACTION=SI1, TYPE=NODE TO SURFACE", "SCON_UP, SCON_LO",
    "*BOUNDARY",
    "NFIX_HEX8, 1, 3", "NFIX_HEX20, 1, 3", "NFIX_TET4, 1, 3", "NFIX_TET10, 1, 3",
    "NFIX_WEDGE6, 1, 3", "NFIX_WEDGE15, 1, 3", "NFIX_S4, 1, 6", "NFIX_S8, 1, 6", "NFIX_S3, 1, 6", "NFIX_S6, 1, 6",
    "NFIX_B31, 1, 6", "NFIX_B32, 1, 6", "NFIX_TRUSS, 1, 3", "NTIP_TRUSS, 2, 2",
    "NSPRING_FIX, 1, 3", "NSPRING_LOAD, 2, 3", "NSPRING1, 2, 3", "NGAP_LOAD, 2, 3", "NGAP_FIX, 1, 3",
    "NFIX_TIE, 1, 3", "NFIX_CON, 1, 3", "NCON_UP_SIDE, 1, 2",
    f"{R1}, 2, 3", f"{R1rot}, 1, 3", f"{R2}, 2, 2", f"{R3}, 1, 2",
    "*STEP", "*STATIC", "1., 1.",
    "*CLOAD",
    f"{R1}, 1, 2000.", f"{R2}, 1, 1500.", f"{R2}, 3, -500.",
    f"{apex4}, 1, 100.", f"{apex10}, 1, 100.", f"{R3}, 3, -300.",
    "NLOAD_WEDGE6, 1, 50.", "NTIP_B31, 3, -20.", "NTIP_B32, 3, -20.", "NTIP_B32, 4, 5.",
    "NTIP_TRUSS, 3, -100.", "NSPRING_LOAD, 1, 30.", "NSPRING1, 1, -20.", "NGAP_LOAD, 1, 40.",
    "*DLOAD", "EHEX8_TOP, P2, 1.5", "ETIE_UP, P2, 2.", "ECON_UP, P2, 3.", "ES4, P, 0.02", "ES8, P, 0.02",
    "*NODE FILE", "U, RF", "*EL FILE", "S, E, ERR", "*CONTACT FILE", "CDIS, CSTR",
    "*EL PRINT, ELSET=EHEX8", "S, COORD", "*NODE PRINT, NSET=NFIX_HEX8", "RF",
    "*END STEP",
    "*STEP", "*FREQUENCY", "8", "*NODE FILE", "U", "*END STEP",
]
# omitted parts: drop the body lines that mention them
tags = {"beam": ("EB31", "EB32", "B31", "B32"), "truss": ("ET3D2", "TRUSS"), "gap": ("EGAP", "NGAP", "*GAP"),
        "contact": ("ECON", "NCON", "SCON", "NFIX_CON", "CONTACT", "SURFACE INTERACTION", "SURFACE BEHAVIOR", "ESOFT", "1e5"),
        "rigid": ("RIGID BODY",), "coupling": ("*COUPLING", "*KINEMATIC", "1, 3"),
        "dcoup": ("DISTRIBUTING", "EDC"), "eq": ("*EQUATION", "2", f"{apex4}, 3, 1."), "tie": ("*TIE", "STIE_UP, STIE_LO"),
        "nlgeom": (", NLGEOM",)}
for part, words in tags.items():
    if want(part):
        continue
    if part == "nlgeom":
        out = [l.replace(", NLGEOM", "") for l in out]
        continue
    if part == "coupling":
        out = [l for l in out if not (l.startswith("*COUPLING") or l == "*KINEMATIC" or l == "1, 3")]
        continue
    if part == "eq":
        i = out.index("*EQUATION"); del out[i:i + 3]
        continue
    if part == "rigid":
        out = [l for l in out if not l.startswith("*RIGID BODY")]
        # the rigid ref nodes' BCs / loads refer to R1: keep them (free nodes with a load would be singular)
        out = [l for l in out if not (l.startswith(f"{R1},") or l.startswith(f"{R1rot},"))]
        continue
    if part == "dcoup":
        out = [l for l in out if not (l.startswith("*DISTRIBUTING") or l.startswith(f"{R3},") or
                                      any(l.startswith(f"{n}, 1.") for n in dc_nodes))]
        continue
    out = [l for l in out if not any(w in l for w in words)]
here = os.path.dirname(os.path.abspath(__file__))
path = OUT or os.path.join(here, "..", "samples", "elements", "elements.inp")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w") as f:
    f.write("\n".join(out) + "\n")
print(f"{path}: {len(nodes)} nodes, {len(elems)} elements")
