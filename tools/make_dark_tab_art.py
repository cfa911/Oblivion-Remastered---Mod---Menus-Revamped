"""The Dark tab compiler (used by tools/make_dark_pack.py for the Inventory and Magic tab atlases) and the Magic page
(assets/Skin/MagicPage.png, a run-time swap: T_MagicBackground is shared with Quest/Help/Stats, so it is not in the pack yet).

Usage: python tools/make_dark_tab_art.py <folder of stock .dds exports> [<output folder>]
Export with UE4-DDS-Tools (--mode export --export_as dds) after `retoc to-legacy --version UE5_3 --no-shaders <Content/Paks> <out>`
with --filter T_MagicBackground.

Tabs (dark_tab): the Inventory tab process (local compile-outlined-tabs.cjs, RMI-069/070, ported): the stock Magic atlases have the same
layout (2026-11-10 measurement). Flat bodies, the stepped outlined selected tab, glyphs from the clean standalone T_Category
masks (no parchment noise) and drawn arrows. Greys from src/core/DarkStyle.h `tab` (two-tone art is baked: run this again after
changing them). The strip is opaque like the Inventory atlases (the stock Magic ones have see-through wedges there); the
stock alpha is kept below it (the fade into the title band).

Page: the installed Inventory panel art's recipe (MRSkinPlayerPanel_P T_UI_PlayerMenu_Background, fitted on the 2026-11-10
export, RMS 0.6): stock brightness <= 40 -> DarkStyle.h frame, >= 124 -> panel, linear between (the ornament bar keeps its
relief), stock alpha kept; drawn untinted. The Magic details page is the game's own page widget, tinted in code.
Requires Pillow.
"""
import math
import re
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_dark_skin_art import load  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]

# Inventory compiler geometry (the Magic atlases match it)
STRIP = 98
SEL_TOP, BOX_TOP, SIDE, W, RADIUS, GAPHALF, OVERLAP = 4, 10, 4, 2, 6, 2, 8
CENTERS = (173, 343, 516, 688, 860)
EDGES = (0, 260, 432, 603, 774, 1024)
ARROWS = (108, 235, 279, 405, 450, 577, 623, 750, 795, 921)
GLYPH = 64
MID = round((BOX_TOP + STRIP) / 2)
GLYPH_TOP = MID - GLYPH // 2


def tab_values():
    text = (ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8")
    block = re.search(r"struct Tab\s*\{(.*?)\}\s*tab;", text, re.S)
    if not block:
        sys.exit("DarkStyle.h has no Style::Tab")
    values = dict((k, float(v)) for k, v in re.findall(r"float\s+(\w+)\s*=\s*([\d.]+)", block.group(1)))
    return values


def rounded_inside(x, y, l, r, t, b):
    if x < l or x >= r or y < t or y >= b:
        return False
    cx = l + RADIUS if x < l + RADIUS else (r - RADIUS - 1 if x >= r - RADIUS else x)
    if y < t + RADIUS and cx != x:
        return math.hypot(x - cx, y - (t + RADIUS)) <= RADIUS
    return True


def shape(active, x, y, v):
    if y >= STRIP:
        return v["selected"]
    sel_l, sel_r = max(SIDE, EDGES[active] - OVERLAP), min(1024 - SIDE, EDGES[active + 1] + OVERLAP)
    if rounded_inside(x, y, sel_l, sel_r, SEL_TOP, STRIP + W):
        return v["selected"] if rounded_inside(x, y, sel_l + W, sel_r - W, SEL_TOP + W, STRIP + W) else v["outline"]
    for i in range(5):
        if i == active:
            continue
        l, r = max(SIDE, EDGES[i] + GAPHALF), min(1024 - SIDE, EDGES[i + 1] - GAPHALF)
        if rounded_inside(x, y, l, r, BOX_TOP, STRIP):
            return v["unselected"] if rounded_inside(x, y, l + W, r - W, BOX_TOP + W, STRIP - W) else v["outline"]
    return v["outline"] if y >= STRIP - W else v["gap"]


def dark_tab(active, stock, masks, v, opaque_strip=True):
    alpha = stock.getchannel("A")
    out = Image.new("RGBA", stock.size)
    px = out.load()
    for y in range(stock.size[1]):
        for x in range(stock.size[0]):
            base = shape(active, x, y, v)
            opacity, selected = 0, False
            if y < STRIP + 4:
                index = next((i for i, c in enumerate(CENTERS) if abs(x - c) < 42), -1)
                arrow = next((i for i, c in enumerate(ARROWS) if abs(x - c) < 16), -1)
                if index >= 0:
                    selected = index == active
                    sx, sy = x - (CENTERS[index] - GLYPH // 2), y - GLYPH_TOP
                    if 0 <= sx < GLYPH and 0 <= sy < GLYPH:
                        opacity = masks[index].getpixel((int((sx + .5) * 256 / GLYPH), int((sy + .5) * 256 / GLYPH)))
                elif arrow >= 0:
                    ax = (x - ARROWS[arrow]) * (1 if arrow % 2 == 0 else -1)
                    ay = y - MID
                    if -9 <= ax <= 10 and abs(ay) <= (10 - ax) * 0.57 and math.hypot(ax + 4, ay) > 3:
                        opacity = 255
                    selected = arrow // 2 == active
            ink = v["glyph"] if selected else v["glyphDim"]
            g = round(base + (ink - base) * opacity / 255)
            # the strip is opaque like the Inventory atlases (the stock Magic ones have see-through wedges there); stock fade below
            # (opaque_strip=False keeps the stock alpha there too: the Inventory atlases in the Dark pack)
            px[x, y] = (g, g, g, 255 if opaque_strip and y < STRIP + 4 else alpha.getpixel((x, y)))
    return out


def style_gray(name):
    text = (ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8")
    found = re.search(r"Rgb\s+" + name + r"\{\s*([\d.]+)", text)
    if not found:
        sys.exit(f"DarkStyle.h has no {name}")
    return float(found.group(1))


DARK, LIGHT = 40, 124   # the Inventory curve's ends (stock brightness)


def inventory_page(image, frame, panel):
    out = []
    for p in image.get_flattened_data():
        lum = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]
        t = min(1.0, max(0.0, (lum - DARK) / (LIGHT - DARK)))
        g = round(frame + (panel - frame) * t)
        out.append((g, g, g, p[3]))
    result = Image.new("RGBA", image.size)
    result.putdata(out)
    return result


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    source = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "assets/Skin"
    inventory_page(load(source / "T_MagicBackground.dds"), style_gray("frame"), style_gray("panel")).save(out / "MagicPage.png")
    print("T_MagicBackground -> MagicPage.png (Inventory panel curve)")


if __name__ == "__main__":
    main()
