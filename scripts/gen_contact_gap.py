#!/usr/bin/env python3
"""gen_contact_gap.py -- write samples/contact_gap/*.inp: a block standing tilted on
a plate, so the gap between them varies, to check how ccxview draws a contact gap
(the contact elements of the .cel, COPEN). Solved by ccx:
scripts/solve_showcase.sh contact_gap contact_gap_c0 contact_gap_over
Checked by scripts/contact_gap_check.py.

mm, N, MPa, steel.  The plate 30 x 20 x 6 (x, y, z), its top at z = 0, its bottom
held. The block 20 x 12 x 10, turned about the y axis through its edge at
x = -10 so that its bottom rises from GAP0 at x = -10 to GAP0 + 0.5 at x = +10
(a tilt of atan(0.5 / 20), 1.4 degrees). Its top is pushed down PUSH in three
increments: the gap closes from the low edge on, part of it stays open.
Node to surface, the block's bottom the slave (its mesh finer than the plate's,
and offset from it), LINEAR pressure-overclosure.

contact_gap        the gap 0 .. 0.5, c0 left at ccx's default
contact_gap_c0     the same with c0 = 1 (contact springs generated, so contact
                   elements written, for slave nodes up to 1 mm off the master)
contact_gap_over   the low edge 0.03 into the plate (overclosure 0.03, gap up to 0.47), c0 = 1

*NODE FILE, CONTACT ELEMENTS makes ccx write jobname.cel; *CONTACT FILE CDIS,
CSTR the CONTACT block of the .frd (COPEN, CSLIP, CPRESS, CSHEAR).
"""
import math
import os

RISE = 0.5                     # the gap's growth over the block's length (20)
PUSH = 0.15                    # the block top's travel
OVER = 0.03                    # contact_gap_over: the low edge this far into the plate
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples", "contact_gap")


def wrap(vals, per=16):
    vals = list(vals)
    return [", ".join(map(str, vals[i:i + per])) + ("," if i + per < len(vals) else "")
            for i in range(0, len(vals), per)]


def deck(name, gap0, c0):
    nodes = {}

    def grid(nx, ny, nz, fx):
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

    PX, PY, PZ = 9, 6, 2
    BX, BY, BZ = 10, 6, 4
    pidx, pel = grid(PX, PY, PZ, lambda u, v, w: (-15 + 30 * u, -10 + 20 * v, -6 + 6 * w))
    a = math.atan2(RISE, 20.0)
    ca, sa = math.cos(a), math.sin(a)

    def block(u, v, w):
        x, y, z = 20 * u, -6 + 12 * v, 10 * w      # from the low edge, upright
        return (-10 + x * ca - z * sa, y, gap0 + x * sa + z * ca)
    bidx, bel = grid(BX, BY, BZ, block)

    L = ["*HEADING", "ccxview sample: a tilted block on a plate, a varying contact gap", "*NODE, NSET=NALL"]
    L += [f"{n}, {x:.8g}, {y:.8g}, {z:.8g}" for n, (x, y, z) in nodes.items()]
    eid, pe, be = 0, [], []
    L.append("*ELEMENT, TYPE=C3D8I, ELSET=PLATE")
    for g, c in pel:
        eid += 1
        pe.append((eid, g))
        L.append(", ".join(map(str, [eid] + c)))
    L.append("*ELEMENT, TYPE=C3D8I, ELSET=BLOCK")
    for g, c in bel:
        eid += 1
        be.append((eid, g))
        L.append(", ".join(map(str, [eid] + c)))
    bot = [n for (i, j, k), n in pidx.items() if k == 0]
    top = [n for (i, j, k), n in bidx.items() if k == BZ]
    L += ["*NSET, NSET=NBOTTOM"] + wrap(bot) + ["*NSET, NSET=NTOP"] + wrap(top)
    L += ["*SURFACE, NAME=PLATE_TOP"] + [f"{e}, S2" for e, (i, j, k) in pe if k == PZ - 1]
    L += ["*SURFACE, NAME=BLOCK_BOTTOM"] + [f"{e}, S1" for e, (i, j, k) in be if k == 0]
    beh = "1000000." if c0 is None else f"1000000., 0., {c0:g}"
    L += ["*MATERIAL, NAME=STEEL", "*ELASTIC", "210000., 0.3",
          "*SOLID SECTION, ELSET=PLATE, MATERIAL=STEEL",
          "*SOLID SECTION, ELSET=BLOCK, MATERIAL=STEEL",
          "*SURFACE INTERACTION, NAME=STEEL_ON_STEEL",
          "*SURFACE BEHAVIOR, PRESSURE-OVERCLOSURE=LINEAR", beh,
          "*CONTACT PAIR, INTERACTION=STEEL_ON_STEEL, TYPE=NODE TO SURFACE",
          "BLOCK_BOTTOM, PLATE_TOP",
          "*BOUNDARY", "NBOTTOM, 1, 3", "NTOP, 1, 2",
          "*STEP, INC=100", "*STATIC", "0.34, 1.",
          "*BOUNDARY", "NTOP, 3, 3, %g" % -PUSH,
          "*NODE FILE, CONTACT ELEMENTS", "U",
          "*CONTACT FILE", "CDIS, CSTR",
          "*END STEP"]
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name + ".inp"), "w") as f:
        f.write("\n".join(L) + "\n")


deck("contact_gap", 0.0, None)
deck("contact_gap_c0", 0.0, 1.0)
deck("contact_gap_over", -OVER, 1.0)
