#!/usr/bin/env python3
"""Draw a grayscale e-ink cover for the annotated Benchmark Part I transcript.

Concept: five identical black bars on a baseline under one dashed reference line --
Benchmark's five equal partners and the fund size it refused to grow.
Output: work/bench/cover.jpg (1125x2000, the reader's 9:16 portrait ratio).
"""
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(os.environ.get("ANNOTATE_WORKSPACE", os.getcwd())).resolve()  # workspace: work/
LIT = str(Path(__file__).resolve().parents[2] / "sdcard/metalio/e-ink/fonts_ttf/Literata.ttf")
AVENIR = "/System/Library/Fonts/Avenir Next.ttc"  # macOS system font
W, H = 1125, 2000
INK, PAPER, GREY, MID = 20, 246, 200, 120


def literata(size, weight):
    f = ImageFont.truetype(LIT, size)
    f.set_variation_by_axes([min(72, max(7, size // 4)), weight])
    return f


def avenir(size, index):
    # Avenir Next.ttc faces: 0 Bold, 2 Demi Bold, 5 Medium, 7 Regular (macOS layout)
    return ImageFont.truetype(AVENIR, size, index=index)


def spaced(draw, xy, text, font, fill, tracking, anchor_center=True):
    widths = [draw.textlength(c, font=font) for c in text]
    total = sum(widths) + tracking * (len(text) - 1)
    x, y = xy
    if anchor_center:
        x -= total / 2
    for c, w in zip(text, widths):
        draw.text((x, y), c, font=font, fill=fill)
        x += w + tracking
    return total


def fit(draw, text, maker, max_w, start):
    size = start
    while size > 10:
        f = maker(size)
        if draw.textlength(text, font=f) <= max_w:
            return f
        size -= 4
    return maker(size)


def main():
    img = Image.new("L", (W, H), PAPER)
    d = ImageDraw.Draw(img)
    cx, margin = W // 2, 90

    # top line
    spaced(d, (cx, 120), "ACQUIRED  ·  SEASON 11, EPISODE 4", avenir(30, 2), INK, 5)
    d.line([(margin, 185), (W - margin, 185)], fill=INK, width=3)

    # title
    title = fit(d, "BENCHMARK", lambda s: literata(s, 860), W - 2 * margin, 260)
    d.text((cx, 230), "BENCHMARK", font=title, fill=INK, anchor="mt")
    tb = d.textbbox((cx, 230), "BENCHMARK", font=title, anchor="mt")
    spaced(d, (cx, tb[3] + 40), "PART I", literata(64, 300), INK, 22)

    # chart: five equal bars under a dashed benchmark line
    base_y, bar_h = 1300, 300
    bar_w, gap = 104, 66
    n = 5
    total = n * bar_w + (n - 1) * gap
    x0 = cx - total // 2
    ref_y = base_y - bar_h - 46
    # faint grid
    for k in range(1, 7):
        y = base_y - k * 110
        d.line([(margin, y), (W - margin, y)], fill=GREY, width=1)
    # dashed reference line
    x = margin
    while x < W - margin:
        d.line([(x, ref_y), (min(x + 26, W - margin), ref_y)], fill=INK, width=4)
        x += 44
    # ghost bars: everyone else's funds keep growing...
    for i in range(n):
        bx = x0 + i * (bar_w + gap) + 26
        gh = int(bar_h * (1.25 + 0.30 * i))
        d.rectangle([bx, base_y - gh, bx + bar_w, base_y], outline=150, width=4)
    # ...Benchmark's stays the same size
    for i in range(n):
        bx = x0 + i * (bar_w + gap)
        d.rectangle([bx, base_y - bar_h, bx + bar_w, base_y], fill=INK)
    d.line([(margin, base_y), (W - margin, base_y)], fill=INK, width=4)
    spaced(d, (cx, base_y + 34), "FIVE EQUAL PARTNERS  ·  ONE SMALL FUND", avenir(28, 2), MID, 4)

    # subtitle + hosts
    sub = literata(46, 400)
    for i, line in enumerate(["How a small venture firm", "chose not to build an empire"]):
        d.text((cx, 1520 + i * 64), line, font=sub, fill=INK, anchor="mt")
    d.text((cx, 1680), "Ben Gilbert & David Rosenthal", font=avenir(38, 5), fill=INK, anchor="mt")

    # bottom band
    d.rectangle([0, 1810, W, H], fill=INK)
    spaced(d, (cx, 1862), "ANNOTATED EDITION", avenir(40, 0), PAPER, 9)
    d.text((cx, 1930), "explainers and vocabulary notes in English", font=avenir(28, 7), fill=210, anchor="mt")

    out = ROOT / "work" / "bench" / "cover.jpg"
    img.save(out, "JPEG", quality=92, progressive=False, optimize=True)
    img.resize((W * 684 // 1125, 1216)).save(ROOT / "work" / "bench" / "cover-preview.png")
    print(out, out.stat().st_size)


if __name__ == "__main__":
    main()
