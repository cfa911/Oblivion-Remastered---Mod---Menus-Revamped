"""Make the Dark skin's neutral art (assets/Skin/*.png) from the game's stock textures, keeping each stock alpha.

Usage: python tools/make_dark_skin_art.py <folder of stock .dds exports> [<output folder>]
Export the stock textures with UE4-DDS-Tools (--mode export --export_as dds) after `retoc to-legacy -f <name>`.
The table here must match uistyle::kDarkArt in src/core/UiTheme.h (stock texture -> file). Requires Pillow.
"""
import struct
import sys
from pathlib import Path
from PIL import Image


def load(path):
    data = path.read_bytes()
    if data[84:88] == b"DX10" and struct.unpack_from("<I", data, 128)[0] in (87, 91):   # B8G8R8A8 (raw)
        height, width = struct.unpack_from("<II", data, 12)
        raw = data[148:148 + width * height * 4]
        return Image.frombuffer("RGBA", (width, height), raw, "raw", "BGRA", 0, 1).copy()
    return Image.open(path).convert("RGBA")


def lum(p):
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]


def flat(value, alpha_scale=1.0):
    return lambda p: (value, value, value, round(p[3] * alpha_scale))


def tint(rgb, alpha_scale=1.0):
    return lambda p: (*rgb, round(p[3] * alpha_scale))


def gray(scale, floor=0):
    return lambda p: (*(min(255, round(floor + lum(p) * scale)),) * 3, p[3])


# stock texture -> (file, pixel map). Brown baked into the art cannot be fixed by a Slate tint (it only darkens).
ART = {
    "T_List_Default": ("RowSeparator.png", flat(235)),                       # row separator line: white
    "T_List_equip_V2": ("RowEquipped.png", tint((212, 170, 78), 0.6)),       # equipped row: gold band (owner)
    "T_Property_1_magnitude": ("StatMagnitude.png", flat(190)),              # effect stat icons
    "T_Property_1_Range": ("StatRange.png", flat(190)),
    "T_Property_1_time": ("StatTime.png", flat(190)),
    "T_Property_1_cost": ("StatCost.png", flat(190)),
    "T_inv_details_effects_title_BG": ("TitleBand.png", gray(0.45)),         # Effects / skill title band
    "T_SigilStone_SubNameBG": ("SigilBand.png", gray(0.45)),                 # sigil Offensive / Defensive band
    "T_Category_Armor": ("SigilArmor.png", gray(2.2)),                       # sigil header icons
    "T_Category_Weapons": ("SigilWeapons.png", gray(1.8)),
    "T_CircularProgress_bg": ("RingCentre.png", gray(1.0)),                  # skill ring centre
    "T_Arrow_Empty": ("ButtonArrow.png", flat(200)),                         # action button arrows
    "T_Button_State_Hovered_LeftLineFade": ("ButtonLine.png", flat(235)),    # action button rules: near-white
}


def main():
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[1] / "assets" / "Skin"
    out.mkdir(parents=True, exist_ok=True)
    for stock, (name, fn) in ART.items():
        im = load(src / f"{stock}.dds")
        done = Image.new("RGBA", im.size)
        done.putdata([fn(p) for p in im.getdata()])
        done.save(out / name, optimize=True)
        same = "kept" if all(abs(a - b[3]) <= 1 or fn(b)[3] != b[3] for a, b in zip(done.getchannel("A").getdata(), im.getdata())) else "CHANGED"
        print(f"{stock:40} -> {name:18} {im.size} alpha {same}")


if __name__ == "__main__":
    main()
