"""Draws the in-game Pack Builder's gear button art (assets/Skin/BuilderGear.png and BuilderGearHover.png).

Plain geometry drawn 8x oversize and reduced with Lanczos for clean edges; parchment ink like the menu's tab labels,
amber when hovered (core/UiTheme.h kDark). SPDX-License-Identifier: GPL-3.0-or-later

    python tools/builder_preview/make_gear.py <out folder>
"""
import math
import pathlib
import sys

from PIL import Image, ImageDraw

SIZE = 64
SCALE = 8
INK = (240, 227, 196, 255)     # kDark search ink 0.94/0.89/0.77
HOVER = (209, 148, 56, 255)    # kDark amber 0.82/0.58/0.22


def gear(colour) -> Image.Image:
    big = SIZE * SCALE
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    c = big / 2
    teeth, outer, inner, hole = 8, big * 0.47, big * 0.36, big * 0.15
    points = []
    for i in range(teeth * 4):
        angle = (i / (teeth * 4)) * 2 * math.pi - math.pi / 2
        radius = outer if i % 4 in (1, 2) else inner
        points.append((c + radius * math.cos(angle), c + radius * math.sin(angle)))
    draw.polygon(points, fill=colour)
    draw.ellipse((c - inner * 0.92, c - inner * 0.92, c + inner * 0.92, c + inner * 0.92), fill=colour)
    draw.ellipse((c - hole, c - hole, c + hole, c + hole), fill=(0, 0, 0, 0))
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def main() -> None:
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    gear(INK).save(out / "BuilderGear.png")
    gear(HOVER).save(out / "BuilderGearHover.png")
    print(f"gear art written to {out}")


if __name__ == "__main__":
    main()
