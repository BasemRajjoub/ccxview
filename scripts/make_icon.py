"""Draw the ccxview application icon: "ccx" in yellow over a five-band contour
colour bar, on black. Writes res/ccxview.ico (16..256 px) and res/ccxview.png.
Needs Pillow and a bold sans font (Arial Bold on Windows, DejaVu Sans Bold
elsewhere). Run from the repository root: python scripts/make_icon.py"""
from PIL import Image, ImageDraw, ImageFont

S = 1024                                   # drawn big, scaled down per size
YELLOW, BLACK = (255, 214, 0), (0, 0, 0)
BANDS = [(40, 60, 230), (0, 200, 240), (40, 210, 70), (250, 220, 30), (235, 40, 30)]  # the viewer's default map
FONTS = ["C:/Windows/Fonts/arialbd.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]


def font_path():
    for f in FONTS:
        try:
            ImageFont.truetype(f, 10)
            return f
        except OSError:
            pass
    raise SystemExit("no bold font found: add one to FONTS")


def fit(path, text, width, height):
    """The largest font size whose text fits the box."""
    lo, hi = 10, 1200
    while hi - lo > 1:
        m = (lo + hi) // 2
        b = ImageFont.truetype(path, m).getbbox(text)
        if b[2] - b[0] <= width and b[3] - b[1] <= height: lo = m
        else: hi = m
    return ImageFont.truetype(path, lo)


def draw():
    im = Image.new("RGBA", (S, S), BLACK + (255,))
    d = ImageDraw.Draw(im)
    x0, x1 = S * 0.08, S * 0.92
    f = fit(font_path(), "ccx", x1 - x0, S * 0.40)
    b = f.getbbox("ccx")
    d.text((S / 2 - (b[0] + b[2]) / 2, S * 0.32 - (b[1] + b[3]) / 2), "ccx", font=f, fill=YELLOW)
    w = (x1 - x0) / len(BANDS)
    for i, c in enumerate(BANDS):
        d.rectangle([x0 + i * w, S * 0.64, x0 + (i + 1) * w, S * 0.90], fill=c)
    return im


if __name__ == "__main__":
    import os
    os.makedirs("res", exist_ok=True)
    big = draw().resize((256, 256), Image.LANCZOS)
    big.save("res/ccxview.png")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    big.save("res/ccxview.ico", sizes=[(s, s) for s in sizes])
    print("wrote res/ccxview.ico and res/ccxview.png")
