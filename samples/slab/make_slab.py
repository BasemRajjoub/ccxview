"""Concrete slab of S8R shells, for the reinforcement (REBAR) beside the section forces.

4 x 4 m, 200 mm thick, in the x-y plane (normal +z, so "top" is +z), concrete
E = 30 000 MPa, nu = 0.2; mm / N / MPa. Simply supported on all four edges (uz = 0,
the corners held in plane just enough), 12 x 12 elements. One step: a uniform
design pressure of 20 kPa (0.02 MPa) pushing down (-z) on the top (P -0.02:
CalculiX pushes a shell along its normal for a positive P).
Kirchhoff (Timoshenko, square plate, nu = 0.2): Mxx = Myy = 0.0442 q a^2 =
14.1 kN m/m at the middle, the bottom (-z) in tension, so Mxx < 0 there in
ccxview's sign (positive: the +e3 side pulls); twisting at the corners
Mxy = 0.0325 (1 - nu) / 0.7 q a^2 ~ 11.9 kN m/m. With the default cover 40 mm
(z = 120 mm) and fyd = 435 MPa: As_x_bot ~ 14 100 / (120 * 435) = 0.27 mm^2/mm
(270 mm^2/m) at the middle; top steel at the corners from the twist.
Run: python make_slab.py && ccx slab"""

A, T = 4000.0, 200.0
NE = 12                                          # elements per side
Q = 0.02                                         # MPa

n2 = 2 * NE                                      # node grid, quadratic: every half element
node = {}
lines = ["*HEADING", "ccxview sample: S8R concrete slab 4 x 4 m, simply supported, 20 kPa", "*NODE, NSET=NALL"]
for j in range(n2 + 1):
    for i in range(n2 + 1):
        if i % 2 and j % 2:
            continue                             # no node in the middle of an element
        node[i, j] = len(node) + 1
        lines.append("%d, %g, %g, 0" % (node[i, j], A * i / n2, A * j / n2))
lines.append("*ELEMENT, TYPE=S8R, ELSET=SLAB")
e = 0
for b in range(NE):
    for a in range(NE):
        e += 1
        i, j = 2 * a, 2 * b
        c = [node[i, j], node[i + 2, j], node[i + 2, j + 2], node[i, j + 2],
             node[i + 1, j], node[i + 2, j + 1], node[i + 1, j + 2], node[i, j + 1]]
        lines.append("%d, %s" % (e, ", ".join(map(str, c))))
edge = sorted({node[i, j] for (i, j) in node if i in (0, n2) or j in (0, n2)})
lines.append("*NSET, NSET=EDGES")
lines += ["%d," % n for n in edge]
lines += ["*MATERIAL, NAME=CONCRETE", "*ELASTIC", "30000, 0.2",
          "*SHELL SECTION, ELSET=SLAB, MATERIAL=CONCRETE", "%g" % T,
          "*BOUNDARY", "EDGES, 3", "%d, 1, 2" % node[0, 0], "%d, 2" % node[n2, 0],
          "*STEP", "*STATIC", "*DLOAD", "SLAB, P, %g" % -Q,
          "*NODE FILE", "U", "*EL FILE", "S", "*END STEP"]
open("slab.inp", "w").write("\n".join(lines) + "\n")
