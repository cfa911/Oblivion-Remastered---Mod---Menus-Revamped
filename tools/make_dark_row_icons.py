"""Make the Dark Magic row spell-type icons (assets/Skin/MagicRow-<stock>.png).

Usage: python tools/make_dark_row_icons.py <folder of stock .dds exports> [<output folder>]
Export with UE4-DDS-Tools (--mode export --export_as dds) after `retoc to-legacy --version UE5_3 --no-shaders <Content/Paks> <out>`
with --filter _ListSmall.

The Inventory's light-icon recipe (one flat value, stock alpha; RMI-071 make-details-textures-2.py) as pure white shapes; the
game draws them times DarkStyle.h rowIcon, so the colour stays configurable. Only the T_MagicMenuIcon_*_ListSmall icons: they
are shared with Sigil Stone and the effect lists, so they are swapped per widget inside Magic. The Magic-only school icons,
details stat icons and tabs are baked into the Dark pack (tools/make_dark_pack.py). Requires Pillow.
"""
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_dark_skin_art import load  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    source = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "assets/Skin"
    icons = sorted(source.glob("T_MagicMenuIcon_*_ListSmall.dds"))
    if not icons:
        sys.exit("no T_MagicMenuIcon_*_ListSmall exports found")
    for path in icons:
        stock = load(path)
        white = Image.new("RGBA", stock.size, (255, 255, 255, 255))
        white.putalpha(stock.getchannel("A"))
        white.save(out / f"MagicRow-{path.stem}.png")
        print(f"{path.stem} -> MagicRow-{path.stem}.png")


if __name__ == "__main__":
    main()
