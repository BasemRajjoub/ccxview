"""Cantilever plate of S8R shells, for checking the shell section forces.

100 x 20 mm in the x-y plane, t = 2 mm, steel, mm / N / MPa; clamped along
x = 0, 10 x 2 elements. Step 1: 1000 N pull along x at the free end x = 100:
Nxx = P / b = 50 N/mm everywhere, nothing else. Step 2: 20 N down (-z) along
the free end instead, spread consistently over the edge: Mxx = F (L - x) / b,
100 N mm/mm at the root (top fibres in tension), Qx = -F / b = -1 N/mm. The
bending last: the viewer scales the deformation by the last step.
Run: python make_shellplate.py && ccx shellplate"""

L, B, T = 100.0, 20.0, 2.0
NX, NY = 10, 2                                   # elements along x, across y
F, P = 20.0, 1000.0

ix, iy = 2 * NX, 2 * NY                          # node grid, quadratic: every half element
node = {}
lines = ["*HEADING", "ccxview sample: S8R cantilever plate, in-plane pull then end line load", "*NODE, NSET=NALL"]
for j in range(iy + 1):
    for i in range(ix + 1):
        if i % 2 and j % 2:
            continue                             # no node in the middle of an element
        node[i, j] = len(node) + 1
        lines.append("%d, %g, %g, 0" % (node[i, j], L * i / ix, B * j / iy))
lines.append("*ELEMENT, TYPE=S8R, ELSET=EALL")
e = 0
for b in range(NY):
    for a in range(NX):
        e += 1
        i, j = 2 * a, 2 * b
        c = [node[i, j], node[i + 2, j], node[i + 2, j + 2], node[i, j + 2],
             node[i + 1, j], node[i + 2, j + 1], node[i + 1, j + 2], node[i, j + 1]]
        lines.append("%d, %s" % (e, ", ".join(map(str, c))))
lines.append("*NSET, NSET=ROOT")
lines += ["%d," % node[0, j] for j in range(iy + 1)]
lines.append("*NSET, NSET=TIP")
lines += ["%d," % node[ix, j] for j in range(iy + 1)]


def edge_loads(total):
    """consistent loads on the quadratic free edge: 1/6 4/6 1/6 of each element's share"""
    w = [0.0] * (iy + 1)
    for b in range(NY):
        for k, f in enumerate((1 / 6, 4 / 6, 1 / 6)):
            w[2 * b + k] += f * total / NY
    return w


lines += ["*MATERIAL, NAME=STEEL", "*ELASTIC", "210000, 0.3",
          "*SHELL SECTION, ELSET=EALL, MATERIAL=STEEL", "%g" % T,
          "*BOUNDARY", "ROOT, 1, 6",
          "*STEP", "*STATIC", "*CLOAD"]
lines += ["%d, 1, %.9g" % (node[ix, j], w) for j, w in enumerate(edge_loads(P))]
lines += ["*NODE FILE", "U, RF", "*EL FILE", "S", "*NODE PRINT, NSET=ROOT, TOTALS=ONLY", "RF", "*END STEP",
          "*STEP", "*STATIC", "*CLOAD, OP=NEW"]
lines += ["%d, 3, %.9g" % (node[ix, j], -w) for j, w in enumerate(edge_loads(F))]
lines += ["*NODE FILE", "U, RF", "*EL FILE", "S", "*NODE PRINT, NSET=ROOT, TOTALS=ONLY", "RF", "*END STEP"]
open("shellplate.inp", "w").write("\n".join(lines) + "\n")
