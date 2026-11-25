#!/usr/bin/env python3
"""gen_materials.py -- samples/materials: a post on a foundation, seven materials.

One structured mesh of rings (C3D8I, shared nodes at every interface), each part
an element set with its own material, to show the Studio look's material
appearances (ccxview --look studio --faces material):

  FOUNDATION  concrete ring          known only by its constants (E, nu, rho)
  PAD         rubber bearing pad     known only by its constants
  S355        steel flange           by name
  ALU6061     aluminium tube         by name
  CUZN39      brass collar           by name
  PA66        plastic sleeve         by name
  TI6AL4V     titanium cap           by name

Units mm, t, s (N, MPa). Fixed at the bottom; a side load and a twist at the cap.
Run:  python3 scripts/gen_materials.py && (cd samples/materials && ccx materials)
"""
import math, os

R = [20, 25, 30, 35, 40, 45, 50, 55, 60]                       # ring radii
Z = [0, 5, 10, 15, 20, 22, 24, 28, 32, 38, 44, 50, 55, 60, 65, 70, 75, 80,
     85, 90, 95, 100, 105, 110, 114, 118]
NT = 64                                                         # around

# name, r from, r to, z from, z to, *ELASTIC (E, nu), *DENSITY
PARTS = [
    ("FOUNDATION", 20, 60, 0, 20,    (30000., 0.2),   2.4e-9),
    ("PAD",        20, 50, 20, 24,   (5., 0.49),      1.1e-9),
    ("S355",       20, 50, 24, 32,   (210000., 0.3),  7.85e-9),
    ("ALU6061",    20, 30, 32, 110,  (70000., 0.33),  2.7e-9),
    ("CUZN39",     30, 40, 60, 70,   (100000., 0.34), 8.5e-9),
    ("PA66",       30, 35, 80, 95,   (3000., 0.38),   1.14e-9),
    ("TI6AL4V",    20, 40, 110, 118, (113000., 0.34), 4.43e-9),
]

def nid(ir, it, iz):
    return 1 + (iz * len(R) + ir) * NT + (it % NT)

def main():
    out = os.path.join(os.path.dirname(__file__), "..", "samples", "materials")
    os.makedirs(out, exist_ok=True)
    elems, sets, used = [], {}, set()
    for name, r0, r1, z0, z1, _, _ in PARTS:
        ids = sets.setdefault(name, [])
        for ir in range(R.index(r0), R.index(r1)):
            for iz in range(Z.index(z0), Z.index(z1)):
                for it in range(NT):
                    # C3D8: bottom face then top, counter-clockwise seen from outside (+z on top)
                    n = [nid(ir, it, iz), nid(ir + 1, it, iz), nid(ir + 1, it + 1, iz), nid(ir, it + 1, iz),
                         nid(ir, it, iz + 1), nid(ir + 1, it, iz + 1), nid(ir + 1, it + 1, iz + 1), nid(ir, it + 1, iz + 1)]
                    elems.append(n); ids.append(len(elems)); used.update(n)
    L = ["*HEADING", "Post on a foundation: seven materials for the Studio look", "*NODE, NSET=NALL"]
    for iz, z in enumerate(Z):
        for ir, r in enumerate(R):
            for it in range(NT):
                k = nid(ir, it, iz)
                if k in used:
                    a = 2 * math.pi * it / NT
                    L.append(f"{k}, {r * math.cos(a):.6f}, {r * math.sin(a):.6f}, {z:.6f}")
    L.append("*ELEMENT, TYPE=C3D8I, ELSET=EALL")
    L += [f"{i + 1}, " + ", ".join(map(str, e)) for i, e in enumerate(elems)]
    for name, ids in sets.items():
        L.append(f"*ELSET, ELSET=E{name}")
        L += [", ".join(map(str, ids[i:i + 16])) for i in range(0, len(ids), 16)]
    bottom = sorted(k for k in used if (k - 1) // (len(R) * NT) == 0)
    top = sorted(k for k in used if (k - 1) // (len(R) * NT) == len(Z) - 1)
    L.append("*NSET, NSET=NBOT"); L += [", ".join(map(str, bottom[i:i + 16])) for i in range(0, len(bottom), 16)]
    L.append("*NSET, NSET=NTOP"); L += [", ".join(map(str, top[i:i + 16])) for i in range(0, len(top), 16)]
    for name, _, _, _, _, el, rho in PARTS:
        L += [f"*MATERIAL, NAME={name}", "*ELASTIC", f"{el[0]}, {el[1]}", "*DENSITY", f"{rho}",
              f"*SOLID SECTION, ELSET=E{name}, MATERIAL={name}"]
    L += ["*BOUNDARY", "NBOT, 1, 3", "*STEP", "*STATIC", "*CLOAD"]
    # a side load (x) and a twist about z, shared over the cap's top nodes
    xyz = {}
    for iz, z in enumerate(Z):
        for ir, r in enumerate(R):
            for it in range(NT):
                a = 2 * math.pi * it / NT
                xyz[nid(ir, it, iz)] = (r * math.cos(a), r * math.sin(a))
    F, T = 3000.0, 4.0e5
    rr = sum(xyz[k][0] ** 2 + xyz[k][1] ** 2 for k in top)
    for k in top:
        x, y = xyz[k]
        fx = F / len(top) - T * y / rr
        fy = T * x / rr
        L += [f"{k}, 1, {fx:.6g}", f"{k}, 2, {fy:.6g}"]
    L += ["*NODE FILE", "U", "*EL FILE", "S", "*END STEP"]
    with open(os.path.join(out, "materials.inp"), "w") as f:
        f.write("\n".join(L) + "\n")
    print(f"{len(used)} nodes, {len(elems)} elements -> samples/materials/materials.inp")

main()
