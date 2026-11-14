"""Make the Dark sort icons (assets/Sorting/DarkSort/*.png) from the game's stock sort textures, keeping each stock alpha.

Usage: python tools/make_dark_sort_icons.py <folder of stock .dds exports> [<output folder>]
Export the stock textures with UE4-DDS-Tools (--mode export --export_as dds) after
`retoc to-legacy --filter Icon_Stats/T_Property_1_<name>. --version UE5_3 --no-shaders <Content/Paks> <out>`.
White shapes (RGB 255; the game colours them from DarkStyle.h headerIcon, 247 like the old local package) with the stock alpha (shipped as PNGs instead, owner
2026-11-10). The table must match itemsort::DarkTextureFile / DarkAllFile in src/sorting/rules/ItemSort.h. Requires Pillow.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dds_image import load  # noqa: E402

ICONS = {
    # the Inventory six (sorting, Health, Attack, defense, Weight, MoneyV2) are baked into MRDark_P (make_dark_pack.py)
    "T_Property_1_cost": "Sort-Cost.png",
    "T_Property_1_school": "Sort-School.png",
    "T_Property_1_LesserPower": "Sort-Type.png",
    "T_Property_1_goodbad": "Sort-Effect.png",
    "T_Property_1_magnitude": "Sort-Magnitude.png",
}
WHITE = 255


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    source = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[1] / "assets/Sorting/DarkSort"
    out.mkdir(parents=True, exist_ok=True)
    for stock, file in ICONS.items():
        image = load(source / f"{stock}.dds")
        image.putdata([(WHITE, WHITE, WHITE, p[3]) for p in image.get_flattened_data()])
        image.save(out / file)
        print(f"{stock} -> {file} {image.size}")


if __name__ == "__main__":
    main()
