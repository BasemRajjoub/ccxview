#!/usr/bin/env python3
"""gen_pin.py -- write samples/showcase/pin.stl: the parts the showcase plate is
held by but that were not analysed, to show beside its results (Groups >
Imported geometry). A clevis: two cheeks either side of the plate, joined by a
yoke above its top edge, and a pin through the hole with a head and a hex nut.

mm, the plate's frame (gen_showcase.py): plate 200 x 100 x 10 about the origin,
hole radius 20 on the z axis.  Binary STL, outward counter-clockwise triangles.
"""
import math
import os
import struct

OUT = os.environ.get("PIN_OUT") or os.path.join(os.path.dirname(__file__), "..", "samples", "showcase", "pin.stl")
R_PIN, R_HOLE, R_LUG = 19.5, 20.0, 32.0     # pin, the cheeks' bore, the cheeks' round end
Z_IN, Z_OUT = 7.0, 15.0                     # a cheek from 2 mm off the plate's face, 8 thick
Y_YOKE, Y_TOP = 60.0, 75.0                  # the yoke above the plate's top edge (y = 50)
NA = 72                                     # facets round

tris = []


def tri(a, b, c, out):
    """a triangle, turned so that its normal points along out"""
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    if sum(n[i] * out[i] for i in range(3)) < 0:
        b, c = c, b
    tris.append((a, b, c))


def quad(a, b, c, d, out):
    tri(a, b, c, out)
    tri(a, c, d, out)


def prism(ring, z0, z1, centre=None):
    """a closed prism over a convex outline (list of (x, y)), from z0 to z1"""
    cx = sum(p[0] for p in ring) / len(ring) if centre is None else centre[0]
    cy = sum(p[1] for p in ring) / len(ring) if centre is None else centre[1]
    n = len(ring)
    for i in range(n):
        (x0, y0), (x1, y1) = ring[i], ring[(i + 1) % n]
        mx, my = (x0 + x1) / 2 - cx, (y0 + y1) / 2 - cy
        quad((x0, y0, z0), (x1, y1, z0), (x1, y1, z1), (x0, y0, z1), (mx, my, 0))
        tri((cx, cy, z1), (x0, y0, z1), (x1, y1, z1), (0, 0, 1))
        tri((cx, cy, z0), (x0, y0, z0), (x1, y1, z0), (0, 0, -1))


def circle(r, n=NA, phase=0.0):
    return [(r * math.cos(phase + 2 * math.pi * i / n), r * math.sin(phase + 2 * math.pi * i / n)) for i in range(n)]


def lug_hit(th):
    """the cheek's outline along angle th: round below the hole, square up to the yoke"""
    c, s = math.cos(th), math.sin(th)
    if s <= 0:
        return R_LUG
    return min(R_LUG / abs(c) if abs(c) > 1e-12 else 1e30, Y_YOKE / s)


def cheek(z0, z1):
    """a flat ring between the bore and the outline, its walls and faces"""
    corner = math.atan2(Y_YOKE, R_LUG)
    ang = sorted(set([2 * math.pi * i / NA for i in range(NA)] + [corner, math.pi - corner]))
    for i in range(len(ang)):
        a0, a1 = ang[i], ang[(i + 1) % len(ang)]
        p0 = (R_HOLE * math.cos(a0), R_HOLE * math.sin(a0))
        p1 = (R_HOLE * math.cos(a1), R_HOLE * math.sin(a1))
        q0 = (lug_hit(a0) * math.cos(a0), lug_hit(a0) * math.sin(a0))
        q1 = (lug_hit(a1) * math.cos(a1), lug_hit(a1) * math.sin(a1))
        quad((*p0, z1), (*q0, z1), (*q1, z1), (*p1, z1), (0, 0, 1))
        quad((*p0, z0), (*q0, z0), (*q1, z0), (*p1, z0), (0, 0, -1))
        mq = ((q0[0] + q1[0]) / 2, (q0[1] + q1[1]) / 2)
        quad((*q0, z0), (*q1, z0), (*q1, z1), (*q0, z1), (mq[0], mq[1], 0))        # outer wall, outward
        mp = ((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2)
        quad((*p0, z0), (*p1, z0), (*p1, z1), (*p0, z1), (-mp[0], -mp[1], 0))      # the bore, toward its axis


cheek(Z_IN, Z_OUT)
cheek(-Z_OUT, -Z_IN)
box = [(-R_LUG, Y_YOKE), (R_LUG, Y_YOKE), (R_LUG, Y_TOP), (-R_LUG, Y_TOP)]
prism(box, -Z_OUT, Z_OUT)                       # the yoke
prism(circle(R_PIN), -Z_OUT - 9.0, Z_OUT + 4.0, (0, 0))   # the pin, out past the nut
prism(circle(27.0), Z_OUT, Z_OUT + 4.0, (0, 0))           # its head
prism(circle(26.0, 6, math.pi / 6), -Z_OUT - 6.0, -Z_OUT, (0, 0))   # the hex nut

with open(OUT, "wb") as f:
    f.write(b"pin and clevis for the showcase plate, ccxview".ljust(80, b" "))
    f.write(struct.pack("<I", len(tris)))
    for a, b, c in tris:
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        ln = math.sqrt(sum(x * x for x in n)) or 1.0
        f.write(struct.pack("<12fH", *[x / ln for x in n], *a, *b, *c, 0))
print(f"{OUT}: {len(tris)} triangles")
