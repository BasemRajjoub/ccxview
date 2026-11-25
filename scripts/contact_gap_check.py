#!/usr/bin/env python3
"""contact_gap_check.py JOB [JOB ...] -- the gap of every node to surface contact
element ccx wrote, measured from the files, against the COPEN ccx wrote.

JOB is a path without extension (samples/contact_gap/contact_gap): JOB.frd (the
nodes, DISP and CONTACT of each increment) and JOB.cel (*NODE FILE, CONTACT
ELEMENTS). For each increment in the .frd, the .cel's last iteration of its last
attempt (what converged), and for each of its contact elements (C3D6: m1 s m2 m4
s m3, C3D4: m1 m2 m3 s):

  gap     the slave node's signed distance to its master face, both deformed
          (coordinates + DISP, true scale): the node projected onto the face's
          bilinear surface, the distance along the face's normal there, positive
          on the face's outward side (the normal of corners m1 m2 m3 m4 by the
          right-hand rule, as ccx orders a face).
  xi, eta where the projection lies on the face (inside when both within +-1)
  off     the in-plane distance from the projection to the face's centre: what a
          line from the slave node to the face's centre adds to the true gap
  copen   COPEN of the node in the .frd (none when the node has no CONTACT value)

Pure python, no packages. Prints a table per increment and a summary.
"""
import math
import re
import sys


def frd_read(path):
    """nodes {id: (x,y,z)}, results [(step, inc, name, {id: [values]})]"""
    nodes, res = {}, []
    cur, block, name, step, inc = None, None, None, 0, 0
    with open(path) as f:
        for ln in f:
            if ln.startswith("    2C"):
                block = "nodes"
                continue
            if ln.startswith("    1PSTEP"):
                v = ln.split()
                inc, step = int(v[2]), int(v[3])
                continue
            if ln.startswith(" -4"):
                name = ln.split()[1]
                cur = {}
                res.append((step, inc, name, cur))
                block = "res"
                continue
            if ln.startswith(" -3"):
                block = None
                continue
            if ln.startswith(" -1") and block:
                nid = int(ln[3:13])
                vals, s = [], ln[13:].rstrip("\n")
                for k in range(0, len(s), 12):
                    t = s[k:k + 12].strip()
                    if t:
                        vals.append(float(t))
                if block == "nodes":
                    nodes[nid] = tuple(vals[:3])
                else:
                    cur[nid] = vals
    return nodes, res


def cel_read(path):
    """{(step, inc): [(m1..m4 or m1..m3, s)]} for the last attempt and iteration of each increment"""
    sets, cur, nn, buf = {}, None, 0, []

    def flush():
        if cur is None or not buf:
            return
        v = buf[:]
        if nn == 6:
            sets.setdefault(cur, []).append(((v[1], v[3], v[6], v[4]), v[2]))
        elif nn == 4:
            sets.setdefault(cur, []).append(((v[1], v[2], v[3]), v[4]))
    with open(path) as f:
        for ln in f:
            if ln.startswith("*"):
                flush()
                buf = []
                m = re.search(r"TYPE=(\w+).*ELSET=contactelements_st(\d+)_in(\d+)_at(\d+)_it(\d+)", ln, re.I)
                if m:
                    nn = {"C3D6": 6, "C3D4": 4}.get(m.group(1).upper(), 0)
                    cur = tuple(int(m.group(k)) for k in range(2, 6))
                else:
                    cur = None
                continue
            buf += [int(t) for t in ln.replace(",", " ").split()]
    flush()
    last = {}
    for (st, inc, att, it) in sets:
        k = (st, inc)
        if k not in last or (att, it) > last[k]:
            last[k] = (att, it)
    return {k: (a, sets[(k[0], k[1]) + a]) for k, a in last.items()}


def sub(a, b): return [a[i] - b[i] for i in range(3)]
def add(a, b): return [a[i] + b[i] for i in range(3)]
def mul(a, s): return [a[i] * s for i in range(3)]
def dot(a, b): return sum(a[i] * b[i] for i in range(3))
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def norm(a): return math.sqrt(dot(a, a))


def project(p, c):
    """p onto the face of corners c (3: flat triangle, 4: bilinear quad): (gap, xi, eta, foot, centre)"""
    cen = mul([sum(q[i] for q in c) for i in range(3)], 1.0 / len(c))
    if len(c) == 3:
        n = cross(sub(c[1], c[0]), sub(c[2], c[0]))
        n = mul(n, 1 / norm(n))
        g = dot(sub(p, c[0]), n)
        foot = sub(p, mul(n, g))
        return g, 0.0, 0.0, foot, cen
    xi = eta = 0.0
    for _ in range(30):                        # Newton on the tangency conditions
        N = [(1 - xi) * (1 - eta) / 4, (1 + xi) * (1 - eta) / 4, (1 + xi) * (1 + eta) / 4, (1 - xi) * (1 + eta) / 4]
        dxi = [-(1 - eta) / 4, (1 - eta) / 4, (1 + eta) / 4, -(1 + eta) / 4]
        deta = [-(1 - xi) / 4, -(1 + xi) / 4, (1 + xi) / 4, (1 - xi) / 4]
        x = [sum(N[k] * c[k][i] for k in range(4)) for i in range(3)]
        t1 = [sum(dxi[k] * c[k][i] for k in range(4)) for i in range(3)]
        t2 = [sum(deta[k] * c[k][i] for k in range(4)) for i in range(3)]
        r = sub(p, x)
        f1, f2 = dot(r, t1), dot(r, t2)
        a11, a12, a22 = dot(t1, t1), dot(t1, t2), dot(t2, t2)  # Gauss-Newton: the face's curvature dropped
        det = a11 * a22 - a12 * a12
        if det == 0:
            break
        d1 = (a22 * f1 - a12 * f2) / det
        d2 = (a11 * f2 - a12 * f1) / det
        xi += d1
        eta += d2
        if abs(d1) + abs(d2) < 1e-12:
            break
    n = cross(t1, t2)
    n = mul(n, 1 / norm(n))
    return dot(sub(p, x), n), xi, eta, x, cen


def check(job):
    nodes, res = frd_read(job + ".frd")
    cel = cel_read(job + ".cel")
    print(f"=== {job}")
    disp = {(s, i): d for s, i, nm, d in res if nm == "DISP"}
    cont = {(s, i): d for s, i, nm, d in res if nm == "CONTACT"}
    for key in sorted(disp):
        u = disp[key]

        def X(n):
            return add(nodes[n], u.get(n, [0, 0, 0])[:3])
        cp = cont.get(key, {})
        if key not in cel:
            print(f"step {key[0]} inc {key[1]}: no contact elements; {len(cp)} nodes with CONTACT values")
            continue
        (att, it), els = cel[key]
        print(f"step {key[0]} inc {key[1]} (attempt {att}, iteration {it}): {len(els)} contact elements, "
              f"{len(set(s for m, s in els))} distinct slave nodes, {len(cp)} nodes with CONTACT values")
        print(f"  {'slave':>6} {'x0':>7} {'y0':>7} {'gap':>11} {'copen':>11} {'gap-copen':>10} {'cpress':>9}"
              f" {'xi':>6} {'eta':>6} {'off':>6} {'centre-line':>11}")
        rows = []
        for m, s in els:
            g, xi, eta, foot, cen = project(X(s), [X(q) for q in m])
            off = norm(sub(foot, cen))
            line = norm(sub(X(s), cen))
            c = cp.get(s)
            rows.append((s, g, c, xi, eta, off, line))
            co = f"{c[0]:11.3e}" if c else f"{'-':>11}"
            dg = f"{g - c[0]:10.2e}" if c else f"{'-':>10}"
            pr = f"{c[3]:9.1f}" if c else f"{'-':>9}"
            print(f"  {s:6d} {nodes[s][0]:7.2f} {nodes[s][1]:7.2f} {g:11.3e} {co} {dg} {pr}"
                  f" {xi:6.2f} {eta:6.2f} {off:6.2f} {line:11.3f}")
        gs = [r[1] for r in rows]
        closed = [r for r in rows if r[1] <= 0]
        inside = [r for r in rows if abs(r[3]) <= 1 + 1e-6 and abs(r[4]) <= 1 + 1e-6]
        diffs = [abs(r[1] - r[2][0]) for r in rows if r[2]]
        print(f"  gap min {min(gs):.4e} max {max(gs):.4e}; closed (gap <= 0) {len(closed)}, open {len(rows) - len(closed)};"
              f" projection inside its face {len(inside)}/{len(rows)}; off max {max(r[5] for r in rows):.3f},"
              f" centre-line / gap where the gap > 0.01: "
              + (f"{min(r[6] / r[1] for r in rows if r[1] > 0.01):.1f} .. {max(r[6] / r[1] for r in rows if r[1] > 0.01):.0f}"
                 if any(r[1] > 0.01 for r in rows) else "-")
              + (f"; |gap - COPEN| max {max(diffs):.2e}" if diffs else ""))
        notcel = sorted(set(cp) - set(s for m, s in els))
        if notcel:
            print(f"  nodes with CONTACT values but no contact element: {notcel}")
    print()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for j in sys.argv[1:]:
        check(j[:-4] if j.endswith((".frd", ".cel", ".inp")) else j)
