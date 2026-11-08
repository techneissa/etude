# SPDX-License-Identifier: GPL-3.0-or-later
"""The icon's small sizes (16, 24 and 32 px), drawn for the pixel grid instead of scaled down.

Scaling the 1024 px art to 16 px turns the staff lines and the clef's thin curves to grey mush. Here
each size is its own picture:

- the clef is Bravura's gClef thickened (a stroke around the outline), fitted to a whole number of
  pixels tall and wide and set on whole pixels, then its edge is pushed towards solid so the stem and
  the bowl stay two pixels thick;
- no staff lines at 16 and 24 px, two at 32 px, each exactly one pixel row;
- a dark halo one pixel around the clef and a brighter gold, so it stands off the background in any
  of the eight colours.

Needs Pillow; the glyph mask is drawn by the caller's rasteriser (inkscape or rsvg-convert).
"""
import math
import os
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SMALL_SIZES = (16, 24, 32)

# Per size: the clef's height as a share of the icon, the stroke (font units) that thickens it, how
# much wider it is drawn than the font has it (a narrow clef is the first thing to blur), the halo's strength and how many staff lines are drawn.
PLAN = {
    16: {"height": 0.88, "stroke": 66, "stretch": 1.4, "halo": 0.70, "lines": 0},
    24: {"height": 0.86, "stroke": 70, "stretch": 1.30, "halo": 0.65, "lines": 0},
    32: {"height": 0.84, "stroke": 56, "stretch": 1.18, "halo": 0.60, "lines": 2},
}

GOLD_TOP = (255, 236, 150)
GOLD_BOTTOM = (255, 196, 64)
GOLD_RICH_BOTTOM = (255, 178, 30)

MASK_PX = 1024
SUPER = 8  # the squircle is drawn this many times larger and shrunk, for a clean edge


def _hex(colour):
    colour = colour.lstrip("#")
    return tuple(int(colour[i:i + 2], 16) for i in (0, 2, 4))


def clef_mask(raster, glyph, stroke, scale, tx, ty):
    """The thickened clef as a white-on-black 1024 px mask, cropped to its ink."""
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{MASK_PX}" height="{MASK_PX}" viewBox="0 0 {MASK_PX} {MASK_PX}">'
           f'<rect width="{MASK_PX}" height="{MASK_PX}" fill="#000"/>'
           f'<g transform="translate({tx:.2f} {ty:.2f}) scale({scale} {-scale})">'
           f'<path d="{glyph}" fill="#fff" stroke="#fff" stroke-width="{stroke}" stroke-linejoin="round"/></g></svg>')
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "mask.svg")
        png = os.path.join(tmp, "mask.png")
        with open(src, "w") as f:
            f.write(svg)
        raster(src, png, MASK_PX)
        mask = Image.open(png).convert("L")
        return mask.crop(mask.getbbox())


def squircle_alpha(px, n=5.0):
    """The icon shape as an alpha mask, smooth at the edge."""
    big = px * SUPER
    pts = []
    for i in range(360):
        t = 2 * math.pi * i / 360
        c, s = math.cos(t), math.sin(t)
        pts.append((big / 2 + big / 2 * math.copysign(abs(c) ** (2 / n), c),
                    big / 2 + big / 2 * math.copysign(abs(s) ** (2 / n), s)))
    m = Image.new("L", (big, big), 0)
    ImageDraw.Draw(m).polygon(pts, fill=255)
    return m.resize((px, px), Image.LANCZOS)


def background(px, inner, outer):
    """The radial gradient of the big icon, centre high, with the corners pulled darker."""
    inner, outer = _hex(inner), _hex(outer)
    img = Image.new("RGB", (px, px))
    cx, cy, radius = (px - 1) / 2, (px - 1) * 0.38, (px - 1) * 0.82
    pix = img.load()
    for y in range(px):
        for x in range(px):
            t = min(1.0, math.hypot(x - cx, y - cy) / radius)
            pix[x, y] = tuple(round(inner[i] + (outer[i] - inner[i]) * t) for i in range(3))
    return img


def _sharpen(mask):
    """Pushes the antialiased edge towards solid: below a quarter is clear, above three quarters solid."""
    return mask.point(lambda v: max(0, min(255, round((v - 64) * 255 / 128))))


def fitted_clef(mask, px, share, stretch):
    """The clef mask scaled to whole pixels tall and wide and centred on a whole pixel."""
    height = max(1, round(px * share))
    width = max(1, round(mask.width * height / mask.height * stretch))
    if (px - width) % 2:  # centre on a pixel boundary, not inside a pixel
        width += 1 if width < px - 1 else -1
    if (px - height) % 2:
        height += 1 if height < px - 1 else -1
    small = _sharpen(mask.resize((width, height), Image.BOX if height < 40 else Image.LANCZOS))
    out = Image.new("L", (px, px), 0)
    out.paste(small, ((px - width) // 2, (px - height) // 2))
    return out


def small_icon(raster, glyph, key, px, colours, rich=False):
    """One colour at one small size. `colours` is (inner, outer, line, line opacity); `rich` is Virtuoso."""
    plan = PLAN[px]
    inner, outer, line, line_op = colours
    scale, tx, ty = 0.45, 512 - 335.5 * 0.45, 512 + 220 * 0.45
    mask = clef_mask(raster, glyph, plan["stroke"], scale, tx, ty)
    clef = fitted_clef(mask, px, plan["height"], plan["stretch"])
    img = background(px, inner, outer)
    # Two staff lines, on whole pixel rows a third and two thirds of the way down, clear of the clef's stem.
    if plan["lines"]:
        draw = ImageDraw.Draw(img, "RGBA")
        rows = [round(px * 0.34), round(px * 0.62)][: plan["lines"]]
        colour = _hex(line) + (round(255 * min(0.75, line_op + 0.2)),)
        for row in rows:
            draw.line([(2, row), (px - 3, row)], fill=colour, width=1)
    # The halo: the clef grown one pixel, blurred a little, in the dark end of the background.
    halo = clef.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(0.6))
    halo = halo.point(lambda v: round(v * plan["halo"]))
    img.paste(Image.new("RGB", (px, px), _hex(outer)), (0, 0), halo)
    # The gold: a vertical blend, brighter at the top.
    top, bottom = GOLD_TOP, GOLD_RICH_BOTTOM if rich else GOLD_BOTTOM
    gold = Image.new("RGB", (px, px))
    gp = gold.load()
    for y in range(px):
        t = y / (px - 1)
        c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(px):
            gp[x, y] = c
    img.paste(gold, (0, 0), clef)
    out = img.convert("RGBA")
    out.putalpha(squircle_alpha(px))
    return out
