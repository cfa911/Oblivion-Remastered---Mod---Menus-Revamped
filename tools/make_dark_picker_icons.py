"""Make the Dark picker icons (assets/Sorting/DarkInventory/*.png): every stock picker icon in assets/Sorting as a flat
DarkStyle.h pickerIcon shape with the stock alpha. These are the mod's own PNGs, not game textures, so the Dark pack MRDark_P
cannot replace them; sorting::detail::Brush loads the DarkInventory copy first in Dark (RMI-085).

Usage: python tools/make_dark_picker_icons.py   (re-run after changing pickerIcon or adding a picker icon). Requires Pillow.
"""
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def picker_rgb():
    text = (ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8")
    found = re.search(r"Rgb\s+pickerIcon\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", text)
    if not found:
        sys.exit("DarkStyle.h has no Rgb pickerIcon")
    return tuple(int(v) for v in found.groups())


def main():
    rgb = picker_rgb()
    source = ROOT / "assets/Sorting"
    out = source / "DarkInventory"
    out.mkdir(exist_ok=True)
    count = 0
    for png in sorted(source.glob("*.png")):
        image = Image.open(png).convert("RGBA")
        alpha = image.getchannel("A")
        white = Image.new("RGBA", image.size, rgb + (0,))
        white.putalpha(alpha)
        white.save(out / png.name)
        count += 1
    print(f"{count} picker icons -> {out} (RGB {rgb})")


if __name__ == "__main__":
    main()
