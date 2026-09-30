#!/usr/bin/env python3
"""gen_showcase.py -- write samples/showcase/showcase.inp: a steel plate with a
hole, the classic stress-concentration case, meshed in C3D20R as one ring from
the hole out to the plate's edge.  Solved by ccx (scripts/solve_showcase.sh).

mm, N, MPa.  200 x 100 x 10, hole diameter 40, the left end fixed.
  step 1  *STATIC  tension: 100 MPa traction on the right end
  step 2  *STATIC  a pin in the hole: 80 MPa pressure on the hole's right half
  step 3  *FREQUENCY  the first six modes of the cantilevered plate
"""
import math
import os

OUT = os.environ.get("SHOWCASE_OUT")
LX, LY, T, R = 100.0, 50.0, 10.0, 20.0      # half length, half height, thickness, hole radius
NA, NR, NZ = 40, 6, 1                        # around, radial, through the thickness
GRADE = 1.6                                  # radial packing toward the hole

nodes, elems = {}, []
key2id = {}


def P(x, y, z):
    k = (round(x, 5), round(y, 5), round(z, 5))
    if k not in key2id:
        key2id[k] = len(nodes) + 1
        nodes[key2id[k]] = k
    return key2id[k]


def edge_hit(th):
    """distance from the centre to the plate's edge along angle th"""
    c, s = math.cos(th), math.sin(th)
    return min(LX / abs(c) if abs(c) > 1e-12 else 1e30, LY / abs(s) if abs(s) > 1e-12 else 1e30)


def pt(a, r, k):
    """doubled grid indices (a around, r radial, k through) -> x, y, z"""
    th = 2 * math.pi * a / (2 * NA)
    u = (r / (2 * NR)) ** GRADE
    rho = R + (edge_hit(th) - R) * u
    return rho * math.cos(th), rho * math.sin(th), -T / 2 + T * k / (2 * NZ)


def N(a, r, k):
    return P(*pt(a % (2 * NA), r, k))


for ia in range(NA):
    for ir in range(NR):
        for k in range(NZ):
            a, r, c = 2 * ia, 2 * ir, 2 * k
            corners = [(a, r, c), (a, r + 2, c), (a + 2, r + 2, c), (a + 2, r, c),
                       (a, r, c + 2), (a, r + 2, c + 2), (a + 2, r + 2, c + 2), (a + 2, r, c + 2)]
            mids = [(a, r + 1, c), (a + 1, r + 2, c), (a + 2, r + 1, c), (a + 1, r, c),
                    (a, r + 1, c + 2), (a + 1, r + 2, c + 2), (a + 2, r + 1, c + 2), (a + 1, r, c + 2),
                    (a, r, c + 1), (a, r + 2, c + 1), (a + 2, r + 2, c + 1), (a + 2, r, c + 1)]
            elems.append([N(*q) for q in corners + mids])

# faces: hex face 4 is nodes 2-3-7-6 (r+2 side, the outside), face 6 is 4-1-5-8 (the hole)
right, hole_right = [], []
left = {n for n, (x, y, z) in nodes.items() if abs(x + LX) < 1e-6}
for i, c in enumerate(elems, 1):
    outer = [nodes[c[q]] for q in (1, 2, 6, 5)]
    if all(abs(p[0] - LX) < 1e-6 for p in outer):
        right.append(f"{i}, S4")
    inner = [nodes[c[q]] for q in (0, 3, 7, 4)]
    if all(p[0] > 1e-6 and abs(math.hypot(p[0], p[1]) - R) < 1e-4 for p in inner):
        hole_right.append(f"{i}, S6")


def wrap(vals, per=16):
    vals = list(vals)
    return [", ".join(map(str, vals[i:i + per])) + ("," if i + per < len(vals) else "")
            for i in range(0, len(vals), per)]


out = ["*HEADING", "ccxview showcase: plate with a hole, tension, pin load and modes",
       "*NODE, NSET=NALL"]
out += [f"{i}, {x:.6g}, {y:.6g}, {z:.6g}" for i, (x, y, z) in nodes.items()]
out.append("*ELEMENT, TYPE=C3D20R, ELSET=EALL")
for i, c in enumerate(elems, 1):
    out += wrap([i] + c)
out += ["*NSET, NSET=NLEFT"] + wrap(sorted(left))
out += ["*ELSET, ELSET=EHOLE"] + wrap(i for i, c in enumerate(elems, 1) if all(abs(math.hypot(*nodes[c[q]][:2]) - R) < 1e-4 for q in (0, 3, 7, 4)))
out += ["*NSET, NSET=NHOLE"] + wrap(sorted(n for n, (x, y, z) in nodes.items() if abs(math.hypot(x, y) - R) < 1e-4))
out += ["*SURFACE, NAME=SRIGHT, TYPE=ELEMENT"] + right
out += ["*SURFACE, NAME=SHOLE, TYPE=ELEMENT"] + hole_right
out += [
    "*MATERIAL, NAME=STEEL", "*ELASTIC", "210000., 0.3", "*DENSITY", "7.85e-9",
    "*SOLID SECTION, ELSET=EALL, MATERIAL=STEEL",
    "*BOUNDARY", "NLEFT, 1, 3",
    "*STEP", "*STATIC",
    "*DLOAD", "SRIGHT, P, -100.",
    "*NODE FILE", "U, RF", "*EL FILE", "S, E",
    "*EL PRINT, ELSET=EHOLE", "S", "*NODE PRINT, NSET=NLEFT", "RF",
    "*END STEP",
    "*STEP", "*STATIC",
    "*DLOAD, OP=NEW", "SHOLE, P, 80.",
    "*NODE FILE", "U, RF", "*EL FILE", "S",
    "*END STEP",
    "*STEP", "*FREQUENCY", "6",
    "*NODE FILE", "U",
    "*END STEP",
]
here = os.path.dirname(os.path.abspath(__file__))
path = OUT or os.path.join(here, "..", "samples", "showcase", "showcase.inp")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w") as f:
    f.write("\n".join(out) + "\n")
print(f"{path}: {len(nodes)} nodes, {len(elems)} elements, {len(right)} right faces, {len(hole_right)} hole faces")
