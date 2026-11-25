#!/usr/bin/env python3
"""gen_modelchange.py -- write samples/modelchange/: a deck whose model changes from
step to step, driven by amplitudes, and a submodel of it.

modelchange.inp: a bar 100 x 10 x 10 mm (C3D8I) held at x = 0 and propped near its
tip by a block standing on the ground (element set EPROP). A tip force rises with the
amplitude RAMP (step time) and a pressure on the top with GROW (total time).
  step 1  the prop carries the tip; four increments show the loads growing
  step 2  *MODEL CHANGE, TYPE=ELEMENT, REMOVE: the prop is gone, the bar hangs free
  step 3  *MODEL CHANGE, TYPE=ELEMENT, ADD: the prop is back (strain free: it is
          put back as it stood, under the bar that has sunk into it)
The steps are NLGEOM: CalculiX removes and adds elements only in nonlinear steps.

submodel.inp: the root of the bar (x 0 .. 20) on a finer mesh. Its cut at x = 20
follows the global model: step 1 by its displacements (*SUBMODEL, TYPE=NODE and
*BOUNDARY, SUBMODEL), step 2 by its stresses as a pressure on the cut
(*SUBMODEL, TYPE=SURFACE and *DSLOAD, SUBMODEL). Solve modelchange.inp first:
  cd samples/modelchange && ccx modelchange && ccx submodel
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples", "modelchange")


def grid(nx, ny, zs, dx, dy, want):
    """nodes on a grid; elements where want(i, k) (k: the layer between zs[k] and zs[k + 1])"""
    nid = lambda i, j, k: 1 + i + (nx + 1) * (j + (ny + 1) * k)
    used, elems = set(), []
    for k in range(len(zs) - 1):
        for j in range(ny):
            for i in range(nx):
                if not want(i, k):
                    continue
                c = [nid(i, j, k), nid(i + 1, j, k), nid(i + 1, j + 1, k), nid(i, j + 1, k),
                     nid(i, j, k + 1), nid(i + 1, j, k + 1), nid(i + 1, j + 1, k + 1), nid(i, j + 1, k + 1)]
                used.update(c)
                elems.append((len(elems) + 1, i, k, c))
    nodes = {}
    for k, z in enumerate(zs):
        for j in range(ny + 1):
            for i in range(nx + 1):
                n = nid(i, j, k)
                if n in used:
                    nodes[n] = (i * dx, j * dy, z)
    return nodes, elems


def write_nodes(f, nodes):
    f.write("*NODE, NSET=NALL\n")
    for n in sorted(nodes):
        x, y, z = nodes[n]
        f.write("%d, %g, %g, %g\n" % (n, x, y, z))


def ids(f, kw, name, v):
    f.write("*%s, %s=%s\n" % (kw, "NSET" if kw == "NSET" else "ELSET", name))
    v = sorted(v)
    for a in range(0, len(v), 12):
        f.write(", ".join(str(x) for x in v[a:a + 12]) + "\n")


MAT = "*MATERIAL, NAME=STEEL\n*ELASTIC\n210000., 0.3\n*SOLID SECTION, ELSET=EALL, MATERIAL=STEEL\n"
AMPS = ("*AMPLITUDE, NAME=RAMP\n0., 0., 1., 1.\n"
        "*AMPLITUDE, NAME=GROW, TIME=TOTAL TIME\n0., 0., 1., 0.5, 2., 1., 3., 1.\n")


def model():
    zs = [-20, -15, -10, -5, 0, 5, 10]
    nx, ny = 20, 2
    nodes, elems = grid(nx, ny, zs, 5.0, 5.0, lambda i, k: k >= 4 or i >= 16)
    with open(os.path.join(OUT, "modelchange.inp"), "w") as f:
        f.write("*HEADING\nccxview sample: *MODEL CHANGE and amplitudes (scripts/gen_modelchange.py)\n")
        write_nodes(f, nodes)
        f.write("*ELEMENT, TYPE=C3D8I, ELSET=EALL\n")
        for e, i, k, c in elems:
            f.write("%d, %s\n" % (e, ", ".join(map(str, c))))
        ids(f, "ELSET", "EPROP", [e for e, i, k, c in elems if k < 4])
        ids(f, "ELSET", "ETOP", [e for e, i, k, c in elems if k == 5])
        ids(f, "NSET", "NFIX", [n for n, p in nodes.items() if p[0] == 0])
        ids(f, "NSET", "NGROUND", [n for n, p in nodes.items() if p[2] == -20])
        ids(f, "NSET", "NTIP", [n for n, p in nodes.items() if p[0] == 100 and p[2] == 10])
        f.write("*SURFACE, NAME=STOP, TYPE=ELEMENT\nETOP, S2\n")
        f.write(MAT + AMPS)
        f.write("*BOUNDARY\nNFIX, 1, 3\nNGROUND, 1, 3\n")
        out = "*NODE FILE\nU\n*EL FILE\nS\n"                   # small: the sample is kept in the repository
        f.write("*STEP, NLGEOM\n*STATIC, DIRECT\n0.25, 1.\n"
                "*CLOAD, AMPLITUDE=RAMP\nNTIP, 3, -500.\n"
                "*DSLOAD, AMPLITUDE=GROW\nSTOP, P, 2.\n" + out + "*END STEP\n")
        f.write("*STEP, NLGEOM\n*STATIC, DIRECT\n0.5, 1.\n"
                "*MODEL CHANGE, TYPE=ELEMENT, REMOVE\nEPROP\n" + out + "*END STEP\n")
        f.write("*STEP, NLGEOM\n*STATIC, DIRECT\n1., 1.\n"
                "*MODEL CHANGE, TYPE=ELEMENT, ADD=STRAIN FREE\nEPROP\n" + out + "*END STEP\n")


def submodel():
    zs = [0, 2.5, 5, 7.5, 10]
    nx, ny = 8, 4
    nodes, elems = grid(nx, ny, zs, 2.5, 2.5, lambda i, k: True)
    nodes = {n + 10000: p for n, p in nodes.items()}           # not the global model's numbers
    elems = [(e + 10000, i, k, [n + 10000 for n in c]) for e, i, k, c in elems]
    with open(os.path.join(OUT, "submodel.inp"), "w") as f:
        f.write("*HEADING\nccxview sample: a submodel of modelchange.inp (scripts/gen_modelchange.py)\n")
        write_nodes(f, nodes)
        f.write("*ELEMENT, TYPE=C3D8I, ELSET=EALL\n")
        for e, i, k, c in elems:
            f.write("%d, %s\n" % (e, ", ".join(map(str, c))))
        ids(f, "NSET", "NFIX", [n for n, p in nodes.items() if p[0] == 0])
        ids(f, "NSET", "NCUT", [n for n, p in nodes.items() if p[0] == 20])
        ids(f, "ELSET", "ECUT", [e for e, i, k, c in elems if i == nx - 1])
        ids(f, "ELSET", "ETOP", [e for e, i, k, c in elems if k == len(zs) - 2])
        f.write("*SURFACE, NAME=SCUT, TYPE=ELEMENT\nECUT, S4\n")
        f.write("*SURFACE, NAME=STOP, TYPE=ELEMENT\nETOP, S2\n")
        f.write(MAT + AMPS)
        f.write("*SUBMODEL, TYPE=NODE, INPUT=modelchange.frd\nNCUT\n")
        ids(f, "ELSET", "EGLOBAL", range(1, 113))     # the global model's elements, by number (not GENERATE: ccx refuses it)
        f.write("*SUBMODEL, TYPE=SURFACE, INPUT=modelchange.frd, GLOBAL ELSET=EGLOBAL\nSCUT\n")
        f.write("*BOUNDARY\nNFIX, 1, 3\n")
        out = "*NODE FILE\nU, RF\n*EL FILE\nS, E\n"
        f.write("*STEP\n*STATIC\n"
                "*BOUNDARY, SUBMODEL, STEP=1\nNCUT, 1, 3\n"
                "*DSLOAD, AMPLITUDE=GROW\nSTOP, P, 2.\n" + out + "*END STEP\n")
        f.write("*STEP\n*STATIC\n"
                "*BOUNDARY, OP=NEW\nNFIX, 1, 3\n"
                "*DSLOAD, SUBMODEL, STEP=1\nSCUT, P\n" + out + "*END STEP\n")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    model()
    submodel()
