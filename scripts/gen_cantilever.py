#!/usr/bin/env python3
"""gen_cantilever.py -- write samples/cantilever/cantilever.inp: a steel
cantilever of solid rectangular section, C3D20R, under four load cases, to see
what tensor glyphs show (bending: rods along the beam, tension above, compression
below; torsion: shear, discs at 45 degrees on the faces; both together).
Solved by ccx (scripts/solve_showcase.sh cantilever).

mm, N, MPa.  Length 300 along X, 40 wide (Z), 60 high (Y); the root x = 0 fixed.
The free end is tied to a reference node at its centre by a distributing
coupling, so forces and moments go in there without stiffening the end.
  step 1  tip load        Fy = -10 kN                      (sigma ~ 125 MPa at the root)
  step 2  torsion         Mx = 2 kN m                      (tau ~ 90 MPa mid long side)
  step 3  combined        Fy = -10 kN, Mx = 2 kN m, Fx = 100 kN tension
  step 4  pressure        1 MPa on the top face, the end free
"""
import os

L, H, W = 300.0, 60.0, 40.0                  # length (x), height (y), width (z)
NX, NY, NZ = 24, 6, 4                        # elements along each

nodes, elems = {}, []
key2id = {}


def P(a, b, c):
    """doubled grid indices -> node id (created on first use)"""
    k = (a, b, c)
    if k not in key2id:
        key2id[k] = len(nodes) + 1
        nodes[key2id[k]] = (L * a / (2 * NX), -H / 2 + H * b / (2 * NY), -W / 2 + W * c / (2 * NZ))
    return key2id[k]


for i in range(NX):
    for j in range(NY):
        for k in range(NZ):
            a, b, c = 2 * i, 2 * j, 2 * k
            corners = [(a, b, c), (a + 2, b, c), (a + 2, b + 2, c), (a, b + 2, c),
                       (a, b, c + 2), (a + 2, b, c + 2), (a + 2, b + 2, c + 2), (a, b + 2, c + 2)]
            mids = [(a + 1, b, c), (a + 2, b + 1, c), (a + 1, b + 2, c), (a, b + 1, c),
                    (a + 1, b, c + 2), (a + 2, b + 1, c + 2), (a + 1, b + 2, c + 2), (a, b + 1, c + 2),
                    (a, b, c + 1), (a + 2, b, c + 1), (a + 2, b + 2, c + 1), (a, b + 2, c + 1)]
            elems.append(((i, j, k), [P(*q) for q in corners + mids]))

ref = len(nodes) + 1                          # the coupling's reference node, end centre
root = sorted(n for n, (x, y, z) in nodes.items() if abs(x) < 1e-9)
# hex faces: S4 = nodes 2-6-7-3 (x+), S5 = 3-7-8-4 (y+)
end = [f"{e}, S4" for e, ((i, j, k), _) in enumerate(elems, 1) if i == NX - 1]
top = [f"{e}, S5" for e, ((i, j, k), _) in enumerate(elems, 1) if j == NY - 1]


def wrap(vals, per=16):
    vals = list(vals)
    return [", ".join(map(str, vals[i:i + per])) + ("," if i + per < len(vals) else "")
            for i in range(0, len(vals), per)]


out = ["*HEADING", "ccxview sample: cantilever under bending, torsion, both, and pressure",
       "*NODE, NSET=NALL"]
out += [f"{n}, {x:.6g}, {y:.6g}, {z:.6g}" for n, (x, y, z) in nodes.items()]
out += [f"{ref}, {L:.6g}, 0, 0"]
out.append("*ELEMENT, TYPE=C3D20R, ELSET=EALL")
for e, (_, c) in enumerate(elems, 1):
    out += wrap([e] + c)
out += ["*NSET, NSET=NROOT"] + wrap(root)
out += ["*NSET, NSET=NREF", str(ref)]
out += ["*SURFACE, NAME=SEND, TYPE=ELEMENT"] + end
out += ["*SURFACE, NAME=STOP, TYPE=ELEMENT"] + top
out += [
    "*MATERIAL, NAME=STEEL", "*ELASTIC", "210000., 0.3",
    "*SOLID SECTION, ELSET=EALL, MATERIAL=STEEL",
    "*COUPLING, REF NODE=%d, SURFACE=SEND, CONSTRAINT NAME=TIP" % ref, "*DISTRIBUTING", "1, 6",
    "*BOUNDARY", "NROOT, 1, 3",
    "*STEP", "*STATIC",
    "*CLOAD", f"{ref}, 2, -10000.",
    "*NODE FILE", "U, RF", "*EL FILE", "S, E",
    "*END STEP",
    "*STEP", "*STATIC",
    "*CLOAD, OP=NEW", f"{ref}, 4, 2000000.",
    "*NODE FILE", "U, RF", "*EL FILE", "S, E",
    "*END STEP",
    "*STEP", "*STATIC",
    "*CLOAD, OP=NEW", f"{ref}, 1, 100000.", f"{ref}, 2, -10000.", f"{ref}, 4, 2000000.",
    "*NODE FILE", "U, RF", "*EL FILE", "S, E",
    "*END STEP",
    "*STEP", "*STATIC",
    "*CLOAD, OP=NEW",
    "*DLOAD", "STOP, P, 1.",
    "*NODE FILE", "U, RF", "*EL FILE", "S, E",
    "*END STEP",
]
here = os.path.dirname(os.path.abspath(__file__))
path = os.path.join(here, "..", "samples", "cantilever", "cantilever.inp")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w") as f:
    f.write("\n".join(out) + "\n")
print(f"{path}: {len(nodes) + 1} nodes, {len(elems)} elements, {len(end)} end faces, {len(top)} top faces")
