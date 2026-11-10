"""Make the Dark Magic row icons (assets/Skin/MagicRow-<stock>.png), details stat icons (MagicIcon-<stock>.png) and the details
page parchment (assets/Skin/DetailsPage.png).

Usage: python tools/make_dark_row_icons.py <folder of stock .dds exports> [<output folder>]
Export with UE4-DDS-Tools (--mode export --export_as dds) after `retoc to-legacy --version UE5_3 --no-shaders <Content/Paks> <out>`
with --filter T_MagicSchoolSimple_, --filter T_MagicMenuIcon_ (row _ListSmall and details stat icons) and
--filter T_SpellPurchase_LeftPageBG.

Row icons: the Inventory's light-icon recipe (one flat value, stock alpha; RMI-071 make-details-textures-2.py) as pure white
shapes; the game draws them times DarkStyle.h rowIcon, so the colour stays configurable.
DetailsPage.png: the stock parchment unchanged. M_MagicSchoolIcon_paper samples it through a fixed sampler without vertex
colour, so the game's page image gets it as a plain texture and the Inventory page tint (DarkStyle.h detailsPage), which is
what the Inventory page is (M_MaskedPage: this parchment times 0.010). Requires Pillow.
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
    icons = sorted(list(source.glob("T_MagicSchoolSimple_*.dds")) + list(source.glob("T_MagicMenuIcon_*_ListSmall.dds")))
    if not icons:
        sys.exit("no T_MagicSchoolSimple_* / T_MagicMenuIcon_*_ListSmall exports found")
    for path in icons:
        stock = load(path)
        white = Image.new("RGBA", stock.size, (255, 255, 255, 255))
        white.putalpha(stock.getchannel("A"))
        white.save(out / f"MagicRow-{path.stem}.png")
        print(f"{path.stem} -> MagicRow-{path.stem}.png")
    # details stat icons (Spell, Range, Cost, ...): line art whose shape is its alpha, same recipe
    for path in sorted(p for p in source.glob("T_MagicMenuIcon_*.dds") if not p.stem.endswith("_ListSmall")):
        stock = load(path)
        white = Image.new("RGBA", stock.size, (255, 255, 255, 255))
        white.putalpha(stock.getchannel("A"))
        white.save(out / f"MagicIcon-{path.stem}.png")
        print(f"{path.stem} -> MagicIcon-{path.stem}.png")
    page = source / "T_SpellPurchase_LeftPageBG.dds"
    if page.is_file():
        load(page).save(out / "DetailsPage.png")
        print("T_SpellPurchase_LeftPageBG -> DetailsPage.png (stock parchment)")


if __name__ == "__main__":
    main()
