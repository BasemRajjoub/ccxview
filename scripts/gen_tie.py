#!/usr/bin/env python3
"""gen_tie.py -- write samples/tie/tie.inp: a block tied onto a base with a
*TIE whose slave surface reaches past the master, to see ties in ccxview (the
slave and master surfaces, the tied slave nodes and those ccx could not tie).
Solved by ccx (scripts/solve_showcase.sh tie), which writes the untied slave
nodes to tie_WarnNodeMissTiedContact.nam.

mm, N, MPa, steel.  The base 40 x 20 x 10 (x, y, z, z <= 0), its end x = 0
held. The block 30 x 20 x 10 on top of it (z >= 0), from x = 16 to 46: its last
6 mm overhang the base's end. Their meshes do not match (8 x 4 under 11 x 7).
The block's bottom is the slave surface, the base's top the master; the slave
nodes beyond x = 40 find no master face and stay untied. A load pushes the
block's free end down.
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples", "tie")


def wrap(vals, per=16):
    vals = list(vals)
    return [", ".join(map(str, vals[i:i + per])) + ("," if i + per < len(vals) else "")
            for i in range(0, len(vals), per)]


nodes = {}


def grid(nx, ny, nz, fx):
    """a structured hex block: node ids from the next free one; {(i,j,k): id}, [(ijk, 8 nodes)]"""
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
                el.append(((i, j, k), c))
    return idx, el


BX, BY, BZ = 8, 4, 2
UX, UY, UZ = 11, 7, 3
bidx, bel = grid(BX, BY, BZ, lambda u, v, w: (40 * u, 20 * v, -10 + 10 * w))
uidx, uel = grid(UX, UY, UZ, lambda u, v, w: (16 + 30 * u, 20 * v, 10 * w))

L = ["*HEADING", "ccxview sample: a block tied onto a base, its slave surface past the master's end",
     "*NODE, NSET=NALL"]
L += [f"{n}, {x:.6g}, {y:.6g}, {z:.6g}" for n, (x, y, z) in nodes.items()]
eid, be, ue = 0, [], []
L.append("*ELEMENT, TYPE=C3D8I, ELSET=BASE")
for g, c in bel:
    eid += 1
    be.append((eid, g))
    L.append(", ".join(map(str, [eid] + c)))
L.append("*ELEMENT, TYPE=C3D8I, ELSET=BLOCK")
for g, c in uel:
    eid += 1
    ue.append((eid, g))
    L.append(", ".join(map(str, [eid] + c)))
fix = [n for (i, j, k), n in bidx.items() if i == 0]
end = [n for (i, j, k), n in uidx.items() if i == UX]
L += ["*NSET, NSET=NFIX"] + wrap(fix) + ["*NSET, NSET=NEND"] + wrap(end)
# hex faces: S1 = nodes 1-2-3-4 (k-), S2 = 5-6-7-8 (k+)
L += ["*SURFACE, NAME=BASE_TOP"] + [f"{e}, S2" for e, (i, j, k) in be if k == BZ - 1]
L += ["*SURFACE, NAME=BLOCK_BOTTOM"] + [f"{e}, S1" for e, (i, j, k) in ue if k == 0]
L += ["*MATERIAL, NAME=STEEL", "*ELASTIC", "210000., 0.3",
      "*SOLID SECTION, ELSET=BASE, MATERIAL=STEEL",
      "*SOLID SECTION, ELSET=BLOCK, MATERIAL=STEEL",
      "*TIE, NAME=GLUE, POSITION TOLERANCE=0.5", "BLOCK_BOTTOM, BASE_TOP",
      "*BOUNDARY", "NFIX, 1, 3",
      "*STEP", "*STATIC",
      "*CLOAD"] + [f"{n}, 3, -50." for n in end] + [
      "*NODE FILE", "U",
      "*EL FILE", "S",
      "*END STEP"]
os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, "tie.inp"), "w") as f:
    f.write("\n".join(L) + "\n")
