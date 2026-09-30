"""Draw the ccxview application icon: a bent, contour-coloured FE mesh on a dark
rounded tile. Writes res/ccxview.ico (16..256 px) and res/ccxview.png.
Needs Pillow. Run from the repository root: python scripts/make_icon.py"""
import math
from PIL import Image, ImageDraw

S = 1024                                   # drawn big, scaled down per size
NX, NY = 5, 4                              # mesh cells


def rainbow(t):
    """The viewer's default colour map: blue - cyan - green - yellow - red."""
    stops = [(0.0, (40, 60, 230)), (0.25, (0, 200, 240)), (0.5, (40, 210, 70)),
             (0.75, (250, 220, 30)), (1.0, (235, 40, 30))]
    t = min(max(t, 0.0), 1.0)
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if t <= b:
            u = (t - a) / (b - a)
            return tuple(int(ca[i] + u * (cb[i] - ca[i])) for i in range(3))
    return stops[-1][1]


def node(i, j):
    """A cantilever bent upward: x along the beam, y through the depth."""
    u, v = i / NX, j / NY
    x = 0.15 + 0.76 * u
    y = 0.78 - 0.40 * v
    y -= 0.26 * u * u                      # tip deflection
    x += 0.05 * u * u * (v - 0.5)          # the section turns with the slope
    return x * S, y * S


def value(i, j):
    """Bending stress: highest at the clamped end, top and bottom fibres."""
    u, v = i / NX, j / NY
    return (1 - u) * abs(2 * v - 1) * 0.95 + 0.05 * (1 - u)


def draw():
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = S * 0.2
    d.rounded_rectangle([S * 0.03, S * 0.03, S * 0.97, S * 0.97], r, fill=(28, 34, 46, 255))
    d.rounded_rectangle([S * 0.03, S * 0.03, S * 0.97, S * 0.97], r, outline=(70, 82, 104, 255), width=int(S * 0.012))
    # the clamped wall
    d.rectangle([S * 0.09, S * 0.30, S * 0.15, S * 0.86], fill=(120, 130, 150, 255))
    for k in range(6):
        y = S * (0.33 + 0.09 * k)
        d.line([S * 0.09, y, S * 0.055, y + S * 0.04], fill=(120, 130, 150, 255), width=int(S * 0.012))
    # the undeformed outline, dashed
    for i in range(0, 40, 2):
        x0, x1 = 0.15 + 0.76 * i / 40, 0.15 + 0.76 * (i + 1) / 40
        for y in (0.78, 0.38):
            d.line([x0 * S, y * S, x1 * S, y * S], fill=(200, 210, 225, 200), width=int(S * 0.010))
    # cells, each split in 6x6 sub-quads so the colour runs smoothly
    sub = 6
    for i in range(NX):
        for j in range(NY):
            for a in range(sub):
                for b in range(sub):
                    fi = [i + a / sub, i + (a + 1) / sub]
                    fj = [j + b / sub, j + (b + 1) / sub]
                    q = [node(fi[0], fj[0]), node(fi[1], fj[0]), node(fi[1], fj[1]), node(fi[0], fj[1])]
                    c = rainbow(value((fi[0] + fi[1]) / 2, (fj[0] + fj[1]) / 2))
                    d.polygon(q, fill=c + (255,), outline=c + (255,))
    w = int(S * 0.016)
    edge = (20, 24, 32, 255)
    for i in range(NX + 1):
        d.line([node(i, j / 8) for j in range(NY * 8 + 1)], fill=edge, width=w, joint="curve")
    for j in range(NY + 1):
        d.line([node(i / 8, j) for i in range(NX * 8 + 1)], fill=edge, width=w, joint="curve")
    return im


if __name__ == "__main__":
    import os
    os.makedirs("res", exist_ok=True)
    big = draw()
    big.resize((256, 256), Image.LANCZOS).save("res/ccxview.png")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    big.resize((256, 256), Image.LANCZOS).save("res/ccxview.ico", sizes=[(s, s) for s in sizes])
    print("wrote res/ccxview.ico and res/ccxview.png")
