"""Pressure vessel with a hemispherical head and a top nozzle, for trying ASME
VIII-2 5-A stress linearization, history plots and the rest.

Quarter model (symmetry planes x = 0, y = 0, z = 0), C3D20R, mm / N / MPa.
  shell   Ri 500, t 25, 1500 long, tapered 3:1 to the head's 20 at the seam
  head    hemisphere Ri 500, t 20
  nozzle  Ri 150, t 20, 250 high, 15 fillet at the outer weld toe
Steps: 1 design pressure 2 MPa (4 increments), 2 hydrotest 1.25 x 2 MPa,
3 design pressure plus an axial pull on the nozzle (a piping load).
The nozzle's cut end carries the pressure thrust as a traction.

Good lines to linearize (pick an inner-surface node, then the one across
the wall on the outer surface):
  shell far from the head    membrane ~ p Ri / t = 40 MPa hoop, little bending
  shell-head seam            bending from the thickness change
  nozzle-head junction       membrane + bending + peak at the fillet
Run: python make_vessel.py && ccx vessel"""
import math

RI, T_SH, L_SH = 500.0, 25.0, 1500.0
T_HD = 20.0
RN, T_NZ, H_NZ, RF = 150.0, 20.0, 250.0, 15.0
P = 2.0
NT, NK = 4, 8                                    # elements through the wall, around 90 deg
NA, NB, NC, ND = 30, 14, 12, 12                  # shell, head, head-to-nozzle, nozzle


def arc_pts(c, r, a0, a1, n):
    return [(c[0] + r * math.cos(a0 + (a1 - a0) * i / n), c[1] + r * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


def resample(poly, n, grade=1.0):
    """n+1 points along a polyline by arclength; grade > 1 packs them toward the end."""
    d = [0.0]
    for a, b in zip(poly, poly[1:]):
        d.append(d[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    out = []
    for i in range(n + 1):
        u = i / n
        u = 1 - (1 - u) ** grade if grade != 1 else u
        s = u * d[-1]
        j = 1
        while j < len(d) - 1 and d[j] < s:
            j += 1
        f = (s - d[j - 1]) / max(d[j] - d[j - 1], 1e-12)
        a, b = poly[j - 1], poly[j]
        out.append((a[0] + f * (b[0] - a[0]), a[1] + f * (b[1] - a[1])))
    return out


# profile curves (r, z), inner and outer, cut into matching segments
RO_HD = RI + T_HD
taper = 3 * (T_SH - T_HD)
phi_b = math.radians(50)                         # head part meshed radially up to here
# nozzle-head: inner corner where the neck bore meets the sphere
phi0 = math.acos(RN / RI)
z_in_corner = L_SH + RI * math.sin(phi0)
# outer: fillet tangent to the sphere and to the neck's outer wall
rc = RN + T_NZ + RF
zc = L_SH + math.sqrt((RO_HD + RF) ** 2 - rc ** 2)
ang_s = math.atan2(zc - L_SH, rc)                # sphere tangent point direction from the head centre
t_sph = (RO_HD * math.cos(ang_s), L_SH + RO_HD * math.sin(ang_s))
t_neck = (RN + T_NZ, zc)
z_top = zc + H_NZ

seg_in, seg_out = [], []
seg_in.append([(RI, 0.0), (RI, L_SH)])
seg_out.append([(RI + T_SH, 0.0), (RI + T_SH, L_SH - taper), (RO_HD, L_SH)])
seg_in.append(arc_pts((0, L_SH), RI, 0, phi_b, 40))
seg_out.append(arc_pts((0, L_SH), RO_HD, 0, phi_b, 40))
seg_in.append(arc_pts((0, L_SH), RI, phi_b, phi0, 40) + [(RN, zc)])
fil = arc_pts((rc, zc), RF, math.pi + ang_s, math.pi, 20)   # from the sphere side to the neck side
seg_out.append(arc_pts((0, L_SH), RO_HD, phi_b, ang_s, 40) + fil[1:])
seg_in.append([(RN, zc), (RN, z_top)])
seg_out.append([t_neck, (RN + T_NZ, z_top)])

# half stations (element mid nodes on the curves too)
inner, outer = [], []
for (si, so, n, g) in zip(seg_in, seg_out, (NA, NB, NC, ND), (1.6, 1.0, 1.0, 1.0)):
    a, b = resample(si, 2 * n, g), resample(so, 2 * n, g)
    if inner:
        a, b = a[1:], b[1:]
    inner += a
    outer += b
NS = (len(inner) - 1) // 2                       # elements along the profile

nid = {}
nodes = []


def node(I, J, K):
    key = (I, J, K)
    if key not in nid:
        f = J / (2 * NT)
        r = inner[I][0] + f * (outer[I][0] - inner[I][0])
        z = inner[I][1] + f * (outer[I][1] - inner[I][1])
        th = math.radians(90.0) * K / (2 * NK)
        nid[key] = len(nodes) + 1
        nodes.append((r * math.cos(th), r * math.sin(th), z))
    return nid[key]


# C3D20R numbering in local (a, b, c): a along the profile, b through the wall, c around
C = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]
M = [(.5, 0, 0), (1, .5, 0), (.5, 1, 0), (0, .5, 0), (.5, 0, 1), (1, .5, 1), (.5, 1, 1), (0, .5, 1),
     (0, 0, .5), (1, 0, .5), (1, 1, .5), (0, 1, .5)]
# (a, b, c) -> (s, wall, theta) is left handed here: swap to (a, c, b) order if needed
elems = []
for i in range(NS):
    for j in range(NT):
        for k in range(NK):
            loc = C + M
            ids = [node(2 * i + int(2 * a), 2 * j + int(2 * b), 2 * k + int(2 * c)) for (a, b, c) in loc]
            elems.append(ids)


def jac_sign(e):
    p = [nodes[n - 1] for n in e]
    u = [p[1][q] - p[0][q] for q in range(3)]
    v = [p[3][q] - p[0][q] for q in range(3)]
    w = [p[4][q] - p[0][q] for q in range(3)]
    return (u[0] * (v[1] * w[2] - v[2] * w[1]) - u[1] * (v[0] * w[2] - v[2] * w[0]) + u[2] * (v[0] * w[1] - v[1] * w[0]))


flip = jac_sign(elems[0]) < 0
if flip:                                         # mirror in c: swap the bottom and top layers
    perm = [4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11, 16, 17, 18, 19]
    elems = [[e[q] for q in perm] for e in elems]
# faces: b = 0 (inner) is nodes 1-2-6-5 = S3; a = 1 at the last station (nozzle end) is 2-3-7-6 = S4
# (the flip swaps c, which keeps b and a faces the same numbers)

with open("vessel.inp", "w") as o:
    o.write("** pressure vessel: hemispherical head with a top nozzle (quarter model), from make_vessel.py\n")
    o.write("*HEADING\nvessel with nozzle, ASME VIII-2 5-A linearization example\n")
    o.write("*NODE, NSET=NALL\n")
    for n, (x, y, z) in enumerate(nodes, 1):
        o.write(f"{n}, {x:.6f}, {y:.6f}, {z:.6f}\n")
    o.write("*ELEMENT, TYPE=C3D20R, ELSET=EALL\n")
    for e, ids in enumerate(elems, 1):
        s = [str(e)] + [str(q) for q in ids]
        o.write(", ".join(s[:16]) + ",\n" + ", ".join(s[16:]) + "\n")

    def elset(name, pred):
        o.write(f"*ELSET, ELSET={name}\n")
        ids = [e for e in range(1, len(elems) + 1) if pred((e - 1) // (NT * NK))]
        for q in range(0, len(ids), 16):
            o.write(", ".join(map(str, ids[q:q + 16])) + ",\n")
    elset("SHELL", lambda i: i < NA)
    elset("HEAD", lambda i: NA <= i < NA + NB + NC)
    elset("NOZZLE", lambda i: i >= NA + NB + NC)

    def nset(name, pred):
        ids = [n for (I, J, K), n in nid.items() if pred(I, J, K)]
        o.write(f"*NSET, NSET={name}\n")
        for q in range(0, len(ids), 16):
            o.write(", ".join(map(str, sorted(ids)[q:q + 16])) + ",\n")
    nset("SYM_Z", lambda I, J, K: I == 0)
    nset("SYM_Y", lambda I, J, K: K == 0)
    nset("SYM_X", lambda I, J, K: K == 2 * NK)
    o.write("*SURFACE, NAME=INSIDE\n")
    for e in range(1, len(elems) + 1):
        if ((e - 1) // NK) % NT == 0:
            o.write(f"{e}, S3\n")
    o.write("*SURFACE, NAME=NOZZLE_END\n")
    for e in range(1, len(elems) + 1):
        if (e - 1) // (NT * NK) == NS - 1:
            o.write(f"{e}, S4\n")
    o.write("*MATERIAL, NAME=SA516-70\n*ELASTIC\n200000., 0.3\n*DENSITY\n7.85e-9\n")
    o.write("*SOLID SECTION, ELSET=EALL, MATERIAL=SA516-70\n")
    o.write("*BOUNDARY\nSYM_Z, 3\nSYM_Y, 2\nSYM_X, 1\n")
    thrust = P * RN ** 2 / ((RN + T_NZ) ** 2 - RN ** 2)
    pull = 1.5 * thrust                         # the piping load, on top of the thrust

    def step(title, inc, p, tr):
        o.write(f"** {title}\n*STEP\n*STATIC\n{inc}, 1.\n")
        o.write(f"*DLOAD, OP=NEW\nINSIDE, P, {p}\nNOZZLE_END, P, {-tr:.6f}\n")
        o.write("*NODE FILE\nU\n*EL FILE\nS\n*NODE PRINT, NSET=SYM_Z, TOTALS=ONLY\nRF\n*END STEP\n")
    step("design pressure", 0.25, P, thrust)
    step("hydrotest 1.25 x design", 0.5, 1.25 * P, 1.25 * thrust)
    step("design pressure + nozzle pull", 0.5, P, thrust + pull)

print(f"{len(nodes)} nodes, {len(elems)} elements, Jacobian flip {flip}")
