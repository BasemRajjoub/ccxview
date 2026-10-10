#!/usr/bin/env python3
"""gen_contact.py -- write samples/contact/contact.inp (node to surface) and
samples/contact/contact_s2s.inp (surface to surface): a rocker pressed onto a
plate, to see contact in ccxview (contact elements from the .cel, the CONTACT
results, the slave and master surfaces, the model see-through).
Solved by ccx: scripts/solve_showcase.sh contact contact_s2s

mm, N, MPa, steel.  The plate 60 x 40 x 12 (x, y, z), its bottom held. The
rocker 30 x 24, 15 high, its bottom a shallow parabola z = C x^2 touching the
plate at x = 0; its top pushed down 0.04 in three increments, so the contact
strip widens from increment to increment (one increment for surface to surface:
ccx writes each of its contact elements once per integration point, tens of
times, and the .cel grows quickly). The rocker's mesh is finer than the
plate's and offset from it (ccx's node to surface contact stalls on slave nodes
sitting on master nodes): the rocker's bottom is the slave surface.

*NODE FILE, CONTACT ELEMENTS makes ccx write contact.cel, the contact elements
of every iteration; *CONTACT FILE CDIS, CSTR the CONTACT block of the .frd
(COPEN, CSLIP, CPRESS, CSHEAR on the slave nodes).
"""
import os

C = 0.0005                     # rocker bottom curvature: z = C x^2
PUSH = 0.04                    # the rocker top's travel
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples", "contact")


def wrap(vals, per=16):
    vals = list(vals)
    return [", ".join(map(str, vals[i:i + per])) + ("," if i + per < len(vals) else "")
            for i in range(0, len(vals), per)]


def deck(s2s):
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

    PX, PY, PZ = (12, 8, 3) if not s2s else (12, 8, 2)
    RX, RY, RZ = (15, 7, 5) if not s2s else (10, 3, 3)
    W = 24 if not s2s else 12                       # the rocker's width (y)
    pidx, pel = grid(PX, PY, PZ, lambda u, v, w: (-30 + 60 * u, -20 + 40 * v, -12 + 12 * w))

    def rocker(u, v, w):
        x, y = -15 + 30 * u, -W / 2 + W * v
        zb = C * x * x
        return (x, y, zb + (15 - zb) * w)
    ridx, rel = grid(RX, RY, RZ, rocker)

    name = "contact_s2s" if s2s else "contact"
    L = ["*HEADING", "ccxview sample: a rocker pressed onto a plate, %s contact" %
         ("surface to surface" if s2s else "node to surface"), "*NODE, NSET=NALL"]
    L += [f"{n}, {x:.6g}, {y:.6g}, {z:.6g}" for n, (x, y, z) in nodes.items()]
    eid, pe, re_ = 0, [], []
    L.append("*ELEMENT, TYPE=C3D8I, ELSET=PLATE")
    for g, c in pel:
        eid += 1
        pe.append((eid, g))
        L.append(", ".join(map(str, [eid] + c)))
    L.append("*ELEMENT, TYPE=C3D8I, ELSET=ROCKER")
    for g, c in rel:
        eid += 1
        re_.append((eid, g))
        L.append(", ".join(map(str, [eid] + c)))
    bot = [n for (i, j, k), n in pidx.items() if k == 0]
    top = [n for (i, j, k), n in ridx.items() if k == RZ]
    L += ["*NSET, NSET=NBOTTOM"] + wrap(bot) + ["*NSET, NSET=NTOP"] + wrap(top)
    # hex faces: S1 = nodes 1-2-3-4 (k-), S2 = 5-6-7-8 (k+)
    L += ["*SURFACE, NAME=PLATE_TOP"] + [f"{e}, S2" for e, (i, j, k) in pe if k == PZ - 1]
    L += ["*SURFACE, NAME=ROCKER_BOTTOM"] + [f"{e}, S1" for e, (i, j, k) in re_ if k == 0]
    L += ["*MATERIAL, NAME=STEEL", "*ELASTIC", "210000., 0.3",
          "*SOLID SECTION, ELSET=PLATE, MATERIAL=STEEL",
          "*SOLID SECTION, ELSET=ROCKER, MATERIAL=STEEL",
          "*SURFACE INTERACTION, NAME=STEEL_ON_STEEL",
          "*SURFACE BEHAVIOR, PRESSURE-OVERCLOSURE=LINEAR", "1000000.",
          "*CONTACT PAIR, INTERACTION=STEEL_ON_STEEL, TYPE=%s" % ("SURFACE TO SURFACE" if s2s else "NODE TO SURFACE"),
          "ROCKER_BOTTOM, PLATE_TOP",
          "*BOUNDARY", "NBOTTOM, 1, 3", "NTOP, 1, 2",
          "*STEP, INC=100", "*STATIC", "1., 1." if s2s else "0.34, 1.",
          "*BOUNDARY", "NTOP, 3, 3, %g" % (-PUSH if not s2s else -0.6 * PUSH),
          "*NODE FILE, CONTACT ELEMENTS", "U",
          "*EL FILE", "S",
          "*CONTACT FILE", "CDIS, CSTR",
          "*END STEP"]
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name + ".inp"), "w") as f:
        f.write("\n".join(L) + "\n")


deck(False)
deck(True)
