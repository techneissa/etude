#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds etude's app icons: the eight colours, one for each player title.

The icon is Bravura's treble clef (the SMuFL gClef, SIL OFL 1.1; the outline is kept beside the
sources in app/assets/icon/src/) in pale gold with a soft glow over five staff lines, on a gradient.
Two steps, each runnable alone:

  svg     writes the SVG sources, app/assets/icon/svg/<colour>.svg (1024 px, squircle) and
          <colour>-small.svg (the same with a heavier clef and staff, used at 48 px). The colour table
          below is the only place the colours live.
  render  renders every colour's sizes into app/assets/icon/<colour>/etude-<px>.png
          (16 24 32 48 64 128 256 512 1024), the macOS etude.icns and docs-public/icon.png (the README's)
          of the default colour. 16, 24 and 32 px are drawn for the pixel grid by icon_small.py (a
          thickened clef on whole pixels, no staff lines or two, more contrast); the others come from the
          SVGs. Needs Pillow and inkscape or rsvg-convert; the .icns needs iconutil (macOS) or Pillow.

  sheet   (not part of "all") a contact sheet of the eight colours at 1024, 128 and 16 px, 16 px also
          magnified 8 times, into the path given by --out. Needs Pillow and inkscape or rsvg-convert.
  compare (not part of "all") a contact sheet of the 16 and 32 px icons at true size and magnified, the
          old ones (the PNGs at git ref --old, default origin/main) next to the new, into --out.

Usage: python3 scripts/make-icons.py [svg|render|all|sheet|compare] [--only colour,colour] [--out sheet.png]
The colour keys, their order and the title each one needs are mirrored in core/include/etude/
icon_colour.hpp.
"""
import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, "app", "assets", "icon")
SVG_DIR = os.path.join(ICONS, "svg")
GLYPH_FILE = os.path.join(ICONS, "src", "bravura-gclef-E050.txt")

SIZES = (16, 24, 32, 48, 64, 128, 256, 512, 1024)
SMALL_UP_TO = 48
DEFAULT_COLOUR = "wine"

# key, centre of the background gradient, its edge, staff line colour and opacity.
PLAIN = {
    "wine": ("#cf2d46", "#2a0512", "#f4a9b9", 0.46),       # 1 Beginner
    "teal": ("#22c4b0", "#063a4a", "#a6f2e4", 0.46),       # 2 Student
    "petrol": ("#1a727c", "#03202a", "#8fd8d8", 0.42),     # 3 Performer
    "cobalt": ("#3a62ff", "#0b1560", "#8fa8ff", 0.50),     # 4 Soloist
    "midnight": ("#34457a", "#080c22", "#9db0e6", 0.42),   # 5 Pianist
    "aubergine": ("#5c2452", "#18051a", "#e0a8d8", 0.42),  # 6 Concert Pianist
    "graphite": ("#6e7384", "#14151c", "#cdd3e4", 0.40),   # 7 Maestro
}
ORDER = ("wine", "teal", "petrol", "cobalt", "midnight", "aubergine", "graphite", "black")  # 8 Virtuoso

# Bravura gClef: font units (1000 per em, y up, the G line at y 0, a staff space 250).
GLYPH_X0, GLYPH_X1 = 0, 671
S = 0.45                                   # icon px per font unit
TX = 512 - (GLYPH_X0 + GLYPH_X1) / 2 * S   # the clef's ink is centred across
TY = 512 + 220 * S                         # the G line's y, so the glyph's middle sits at 512
TF = f"translate({TX:.2f} {TY:.2f}) scale({S} {-S})"


def squircle_d(n=5.0, steps=240):
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        x = 512 + 512 * math.copysign(abs(c) ** (2 / n), c)
        y = 512 + 512 * math.copysign(abs(s) ** (2 / n), s)
        pts.append(f"{x:.2f},{y:.2f}")
    return "M" + " L".join(pts) + " Z"


def stops(*items):
    return "".join(f'<stop offset="{o}" stop-color="{c}"' + (f' stop-opacity="{a}"' if a is not None else "") + "/>"
                   for o, c, a in items)


def staff(colour, opacity, thick):
    out = ""
    for k in range(-1, 4):  # k 0 is the G line, -1 the bottom line, 3 the top one
        y = TY - 250 * S * k
        out += (f'<rect x="112" y="{y - thick / 2:g}" width="800" height="{thick:g}" rx="{thick / 2:g}" '
                f'fill="{colour}" opacity="{opacity}"/>')
    return out


def clef_layers(glyph, small, glow_op=0.38, halo=""):
    outline = ' stroke="url(#rimgold)" stroke-width="%.1f" stroke-linejoin="round"' % (15 / S) if small else ""
    return (halo
            + f'<g transform="{TF} translate(0 -{14 / S:.1f})" filter="url(#blur12)" fill="#000" opacity=".40"><path d="{glyph}"/></g>'
            + f'<g transform="{TF}" filter="url(#blur24)" fill="#ffc94d" opacity="{glow_op}"><path d="{glyph}"/></g>'
            + f'<g transform="{TF}" fill="url(#gold)"{outline}><path d="{glyph}"/></g>')


def frame(defs, body):
    d = squircle_d()
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
<defs>
<clipPath id="shape"><path d="{d}"/></clipPath>
<linearGradient id="rim" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="#fff" stop-opacity=".42"/><stop offset=".5" stop-color="#fff" stop-opacity=".04"/><stop offset="1" stop-color="#000" stop-opacity=".30"/></linearGradient>
<linearGradient id="rimgold" x1="0" y1="0" x2="1" y2="1">{stops((0, "#fffbe8", None), (1, "#ffe08a", None))}</linearGradient>
<filter id="blur40" x="-60%" y="-60%" width="220%" height="220%"><feGaussianBlur stdDeviation="40"/></filter>
<filter id="blur24" x="-60%" y="-60%" width="220%" height="220%"><feGaussianBlur stdDeviation="24"/></filter>
<filter id="blur12" x="-60%" y="-60%" width="220%" height="220%"><feGaussianBlur stdDeviation="12"/></filter>
<filter id="blur60" x="-80%" y="-80%" width="260%" height="260%"><feGaussianBlur stdDeviation="60"/></filter>
{defs}
</defs>
<g clip-path="url(#shape)">
{body}
<path d="{d}" fill="none" stroke="url(#rim)" stroke-width="8"/>
</g>
</svg>
'''


def radial(inner, outer):
    return f'<radialGradient id="bg" cx="50%" cy="38%" r="82%">{stops((0, inner, None), (1, outer, None))}</radialGradient>'


GOLD = f'<linearGradient id="gold" x1="0" y1="0" x2="1" y2="1">{stops((0, "#fff3c4", None), (1, "#ffbf47", None))}</linearGradient>'
GOLD_RICH = ('<linearGradient id="gold" x1="0" y1="0" x2="1" y2="1">'
             + stops((0, "#fff5c8", None), (.5, "#ffd04a", None), (1, "#f2a312", None)) + '</linearGradient>')


def icon_svg(key, glyph, small):
    thick = 20 if small else 14
    if key == "black":
        # Virtuoso: gradient black with a glossy obsidian sheen, a faint violet glow and a richer gold.
        defs = (radial("#1e1932", "#010003") + GOLD_RICH
                + f'<linearGradient id="sheen" x1="0" y1="0" x2="0.75" y2="0.95">{stops((0, "#fff", .17), (.30, "#fff", .04), (.48, "#fff", 0), (1, "#fff", 0))}</linearGradient>'
                + f'<linearGradient id="streak" x1="0" y1="0" x2="1" y2="0">{stops((0, "#fff", 0), (.5, "#fff", .06), (1, "#fff", 0))}</linearGradient>')
        halo = f'<g transform="{TF}" filter="url(#blur40)" fill="#ffb800" opacity=".42"><path d="{glyph}"/></g>'
        body = ('<rect width="1024" height="1024" fill="url(#bg)"/>'
                '<ellipse cx="512" cy="540" rx="250" ry="360" fill="#7c4dff" opacity=".20" filter="url(#blur60)"/>'
                '<rect width="1024" height="1024" fill="url(#sheen)"/>'
                '<g transform="rotate(-32 512 512)"><rect x="-200" y="610" width="1500" height="56" fill="url(#streak)"/>'
                '<rect x="-200" y="690" width="1500" height="14" fill="url(#streak)"/></g>'
                + staff("#c3acff", 0.28 if not small else 0.40, thick)
                + clef_layers(glyph, small, halo=halo))
        return frame(defs, body)
    inner, outer, line, op = PLAIN[key]
    defs = radial(inner, outer) + GOLD
    body = ('<rect width="1024" height="1024" fill="url(#bg)"/>'
            + staff(line, op if not small else min(0.7, op + 0.12), thick)
            + clef_layers(glyph, small))
    return frame(defs, body)


def write_svgs(keys):
    glyph = open(GLYPH_FILE).read().strip()
    os.makedirs(SVG_DIR, exist_ok=True)
    for key in keys:
        for small in (False, True):
            name = f"{key}-small.svg" if small else f"{key}.svg"
            with open(os.path.join(SVG_DIR, name), "w") as f:
                f.write(icon_svg(key, glyph, small))
    print(f"svg: {len(keys) * 2} files in {os.path.relpath(SVG_DIR, ROOT)}")


def rasteriser():
    if shutil.which("inkscape"):
        return lambda svg, png, px: subprocess.run(
            ["inkscape", svg, "--export-type=png", f"--export-filename={png}", "-w", str(px), "-h", str(px)],
            check=True, capture_output=True)
    if shutil.which("rsvg-convert"):
        return lambda svg, png, px: subprocess.run(
            ["rsvg-convert", "-w", str(px), "-h", str(px), "-o", png, svg], check=True, capture_output=True)
    sys.exit("make-icons: needs inkscape or rsvg-convert to render the SVGs")


def squeeze(png):
    """Re-saves a PNG with Pillow's optimiser when Pillow is there (smaller, same pixels)."""
    try:
        from PIL import Image
    except ImportError:
        return
    with Image.open(png) as im:
        im = im.convert("RGBA")
        im.save(png, optimize=True)


def colours_of(key):
    """The background and staff colours of `key`, as icon_small takes them."""
    if key == "black":
        return ("#1e1932", "#010003", "#c3acff", 0.28)
    return PLAIN[key]


def render_png(raster, key, px, png, glyph=None):
    """One colour at one size: drawn for the pixel grid at 32 px and below 48, else from its SVG."""
    # Imported here: the svg step needs neither icon_small nor Pillow.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import icon_small
    if px in icon_small.SMALL_SIZES:
        glyph = glyph or open(GLYPH_FILE).read().strip()
        icon_small.small_icon(raster, glyph, key, px, colours_of(key), rich=key == "black").save(png)
    else:
        raster(os.path.join(SVG_DIR, f"{key}-small.svg" if px <= SMALL_UP_TO else f"{key}.svg"), png, px)


def render_set(raster, key, sizes=SIZES):
    out = os.path.join(ICONS, key)
    os.makedirs(out, exist_ok=True)
    for px in sizes:
        png = os.path.join(out, f"etude-{px}.png")
        render_png(raster, key, px, png)
        squeeze(png)
    return key


def write_icns():
    """The macOS icon of the default colour: the ten sizes an .iconset holds, packed by iconutil."""
    raster = rasteriser()
    with tempfile.TemporaryDirectory() as tmp:
        iconset = os.path.join(tmp, "etude.iconset")
        os.makedirs(iconset)
        names = {16: "icon_16x16", 32: "icon_16x16@2x", 64: "icon_32x32@2x", 128: "icon_128x128",
                 256: "icon_128x128@2x", 512: "icon_256x256@2x", 1024: "icon_512x512@2x"}
        for px, name in names.items():
            render_png(raster, DEFAULT_COLOUR, px, os.path.join(iconset, name + ".png"))
        # The same pixels serve the 1x and 2x slots of the sizes that coincide.
        for px, name in ((32, "icon_32x32"), (256, "icon_256x256"), (512, "icon_512x512")):
            shutil.copy(os.path.join(iconset, names[px] + ".png"), os.path.join(iconset, name + ".png"))
        target = os.path.join(ICONS, "etude.icns")
        if shutil.which("iconutil"):
            subprocess.run(["iconutil", "-c", "icns", iconset, "-o", target], check=True)
        else:
            from PIL import Image
            with Image.open(os.path.join(iconset, "icon_512x512@2x.png")) as big:
                big.save(target, sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])
    print(f"icns: {os.path.relpath(target, ROOT)}")


def write_sheet(out, keys):
    """The colours side by side at the three sizes people meet them at: the 1024 art, the 128 Dock
    size and 16 px true size, with the 16 px magnified so its pixels can be judged."""
    from PIL import Image, ImageDraw, ImageFont
    raster = rasteriser()
    cols = len(keys)
    cell = 300
    big_h, mid_h, small_h = cell, 150, 150
    width, height = cols * cell + 40, 60 + big_h + mid_h + small_h + 40
    sheet = Image.new("RGBA", (width, height), (36, 37, 42, 255))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 20)
    except OSError:
        font = ImageFont.load_default()
    with tempfile.TemporaryDirectory() as tmp:
        for i, key in enumerate(keys):
            x = 20 + i * cell
            pics = {}
            for px in (1024, 128, 16):
                png = os.path.join(tmp, f"{key}-{px}.png")
                render_png(raster, key, px, png)
                pics[px] = Image.open(png).convert("RGBA")
            draw.text((x + 8, 16), f"{i + 1}  {key}", fill=(235, 235, 240), font=font)
            sheet.alpha_composite(pics[1024].resize((cell - 20, cell - 20), Image.LANCZOS), (x, 50))
            sheet.alpha_composite(pics[128], (x, 50 + big_h))
            sheet.alpha_composite(pics[16], (x + 150, 50 + big_h + 128 - 16))
            sheet.alpha_composite(pics[16].resize((128, 128), Image.NEAREST), (x, 50 + big_h + mid_h))
    draw.text((20, height - 30), "1024 (shown scaled) · 128 · 16 true size and magnified 8 times", fill=(170, 172, 182), font=font)
    sheet.convert("RGB").save(out)
    print(f"sheet: {out}")


def write_compare(out, keys, old_ref):
    """The 16 and 32 px icons of every colour, old (the PNGs at `old_ref`) over new, each at true size
    and magnified, so the pixel-fitted art can be judged against what it replaces."""
    import io
    from PIL import Image, ImageDraw, ImageFont
    cell, pad, label_h = 172, 14, 44
    rows = ((16, 8, "16 px"), (32, 4, "32 px"))
    width = 90 + len(keys) * cell
    height = 40 + len(rows) * (2 * (128 + 24) + label_h)
    sheet = Image.new("RGBA", (width, height), (36, 37, 42, 255))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 18)
    except OSError:
        font = ImageFont.load_default()
    top = 36
    for px, zoom, title in rows:
        draw.text((12, top - 28), f"{title}: old above, new below; true size, then magnified {zoom} times",
                  fill=(235, 235, 240), font=font)
        for r, which in enumerate(("old", "new")):
            y = top + r * (px * zoom + 24)
            draw.text((12, y + px * zoom // 2 - 8), which, fill=(170, 172, 182), font=font)
            for i, key in enumerate(keys):
                x = 80 + i * cell
                if which == "old":
                    blob = subprocess.run(["git", "-C", ROOT, "show", f"{old_ref}:app/assets/icon/{key}/etude-{px}.png"],
                                          check=True, capture_output=True).stdout
                    pic = Image.open(io.BytesIO(blob)).convert("RGBA")
                else:
                    pic = Image.open(os.path.join(ICONS, key, f"etude-{px}.png")).convert("RGBA")
                sheet.alpha_composite(pic.resize((px * zoom, px * zoom), Image.NEAREST), (x, y))
                sheet.alpha_composite(pic, (x + px * zoom + 8, y))
        top += 2 * (px * zoom + 24) + label_h
    for i, key in enumerate(keys):
        draw.text((80 + i * cell, height - 24), key, fill=(170, 172, 182), font=font)
    sheet.convert("RGB").save(out)
    print(f"compare: {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("step", nargs="?", default="all", choices=("svg", "render", "all", "sheet", "compare"))
    ap.add_argument("--old", default="origin/main", help="the compare step: the git ref whose PNGs are the old ones")
    ap.add_argument("--out", default="icons-sheet.png", help="the sheet step: where to write the PNG")
    ap.add_argument("--only", default="", help="comma-separated colour keys (default: all eight)")
    args = ap.parse_args()
    keys = [k for k in ORDER if not args.only or k in args.only.split(",")]
    if args.step == "sheet":
        write_sheet(args.out, keys)
        return
    if args.step == "compare":
        write_compare(args.out, keys, args.old)
        return
    if args.step in ("svg", "all"):
        write_svgs(keys)
    if args.step in ("render", "all"):
        raster = rasteriser()
        with ThreadPoolExecutor(max_workers=4) as pool:
            for key in pool.map(lambda k: render_set(raster, k), keys):
                print(f"png: {key}/ {', '.join(map(str, SIZES))}")
        if DEFAULT_COLOUR in keys:
            write_icns()
            shutil.copy(os.path.join(ICONS, DEFAULT_COLOUR, "etude-512.png"), os.path.join(ROOT, "docs-public", "icon.png"))
            print("png: docs-public/icon.png")


if __name__ == "__main__":
    main()
