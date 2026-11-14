#!/usr/bin/env python3
"""Merge Vietnamese glyphs into the EFNT UI fontpack.

The stock pack is built with --from-ef misans_25_2.ef, whose charset has only a
handful of Latin-1 accented letters, so most Vietnamese precomposed letters
(ờ, ế, ự, ạ ...) render as tofu in the UI (library, reader footer, TOC).

This adds them with the exact variant -> TTF mapping of build_misans_mixed.sh:
  25@2, 30@2 = MiSans-Light; 30@4 = MiSans-Semibold
and first checks that re-rendering glyphs already in the pack reproduces them
byte for byte, so the new letters match the existing ones.

  .venv/bin/python merge_vietnamese_into_fontpack.py \
      --base ../../use_font/font.fontpack -o ../../use_font/font.fontpack --verify
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from build_fontpack import make_key, render_glyph, verify_fontpack, write_fontpack
from merge_emoji_into_fontpack import load_fontpack_items

TTF_DIR = Path(__file__).resolve().parent.parent / "ttf-fonts/MiSans"
VARIANTS = {
    (25, 2): TTF_DIR / "MiSans-Light.ttf",
    (30, 2): TTF_DIR / "MiSans-Light.ttf",
    (30, 4): TTF_DIR / "MiSans-Semibold.ttf",
}

# Every letter Vietnamese uses beyond ASCII, plus combining tone marks for
# decomposed (NFD) text.
VIETNAMESE = (
    "ÀÁÂÃÈÉÊÌÍÒÓÔÕÙÚÝàáâãèéêìíòóôõùúý"
    "ĂăĐđĨĩŨũƠơƯư"
    + "".join(chr(c) for c in range(0x1EA0, 0x1EFA))  # Ạ..ỹ
    + "̣̀́̃̉"
    + "₫"
)

# Glyphs that already exist in the stock pack, used to prove the renderer matches.
PROBE = "AaQgé0"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--base", type=Path, required=True)
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()

    import freetype

    items = load_fontpack_items(args.base)
    by_key = {g.key: g for g in items}
    print(f"base items={len(items)}")

    added = 0
    for (size, bpp), ttf in VARIANTS.items():
        face = freetype.Face(str(ttf))
        face.set_pixel_sizes(0, size)

        # 1) reproducibility check against glyphs the vendor already rendered
        for ch in PROBE:
            old = by_key.get(make_key(ord(ch), size, bpp))
            if old is None:
                continue
            new = render_glyph(face, ord(ch), size, bpp)
            same = new is not None and (new.width, new.height, new.x_offset, new.y_offset,
                                        new.advance, new.bitmap) == (
                old.width, old.height, old.x_offset, old.y_offset, old.advance, old.bitmap)
            if not same:
                sys.exit(f"render mismatch for {ch!r} at {size}@{bpp} ({ttf.name}); refusing to merge")
        print(f"  {size}@{bpp} {ttf.name}: probe glyphs reproduce exactly")

        # 2) add missing Vietnamese letters
        n = 0
        for ch in VIETNAMESE:
            key = make_key(ord(ch), size, bpp)
            if key in by_key:
                continue
            g = render_glyph(face, ord(ch), size, bpp)
            if g is None:
                print(f"    {ch!r} U+{ord(ch):04X} not in {ttf.name}")
                continue
            items.append(g)
            by_key[key] = g
            n += 1
        print(f"  {size}@{bpp}: added {n}")
        added += n

    write_fontpack(items, args.output)
    if args.verify:
        verify_fontpack(args.output, [(ord("ờ"), 25, 2), (ord("ự"), 30, 2), (ord("Ế"), 30, 4)])
    print(f"DONE added={added} items={len(items)} size={args.output.stat().st_size}")


if __name__ == "__main__":
    main()
