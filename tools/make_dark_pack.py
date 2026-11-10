"""Build the Dark UI pack (assets/DarkPack/MRDark_P.pak/.utoc/.ucas): stock textures re-coloured from src/core/DarkStyle.h.

Usage:
  python tools/make_dark_pack.py --legacy <dir> --dds <dir> --ddstools <UE4-DDS-Tools/src/main.py> --retoc <retoc.exe> [--out <dir>]

--legacy  a `retoc to-legacy --version UE5_3 --no-shaders <Content/Paks> <dir>` output holding the stock assets below
--dds     UE4-DDS-Tools `--mode export --export_as dds` exports of the same textures

Phase 0 (owner 2026-11-10, dark-ui-pack-plan.md): only T_SpellPurchase_LeftPageBG, the parchment shared by the Inventory and
Magic details, Help, Spell Purchase, Stats page 3 and Quest details. It is baked to what the Inventory details showed at run time:
the stock colour times DarkStyle.h detailsPage (linear multiply), stock alpha kept byte for byte (BC3 alpha blocks).
The plugin copies the pack into Content/Paks/~mods when the preset is Dark (src/core/DarkPack.cpp). Requires Pillow.
"""
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PACK = "MRDark_P"
# stock asset (path under Content) -> recipe
TEXTURES = {
    "Art/UI/Modern/GameMenuLayer/SpellPurchase/T_SpellPurchase_LeftPageBG": "details_page",
}


def style_float(name):
    text = (ROOT / "src/core/DarkStyle.h").read_text(encoding="utf-8")
    found = re.search(r"float\s+" + name + r"\s*=\s*([\d.]+)f?", text)
    if not found:
        sys.exit(f"DarkStyle.h has no float {name}")
    return float(found.group(1))


def linear(v):
    c = v / 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def srgb(l):
    l = max(0.0, min(1.0, l))
    c = l * 12.92 if l <= 0.0031308 else 1.055 * l ** (1 / 2.4) - 0.055
    return round(c * 255)


def header_size(data):
    assert data[:4] == b"DDS "
    if data[84:88] == b"DXT5":
        return 128
    assert data[84:88] == b"DX10" and int.from_bytes(data[128:132], "little") in (77, 78), "not BC3"
    return 148


def recolour(stock_dds, tint, work):
    """RGB = stock colour times tint (linear); the stock BC3 alpha blocks are kept byte for byte."""
    image = Image.open(stock_dds).convert("RGBA")
    table = [srgb(linear(v) * tint) for v in range(256)]
    rgb = Image.merge("RGB", [band.point(table) for band in image.convert("RGB").split()])
    colour_dds = work / "colour.dds"
    rgb.convert("RGBA").save(colour_dds, pixel_format="DXT5")
    stock = stock_dds.read_bytes()
    colour = colour_dds.read_bytes()
    blocks = bytearray(stock[header_size(stock):])
    replacement = colour[header_size(colour):]
    assert len(blocks) == len(replacement), "size mismatch"
    for offset in range(0, len(blocks), 16):
        blocks[offset + 8:offset + 16] = replacement[offset + 8:offset + 16]
    out = work / stock_dds.name
    out.write_bytes(stock[:header_size(stock)] + blocks)
    back = Image.open(out).convert("RGBA")
    assert back.getchannel("A").tobytes() == image.getchannel("A").tobytes(), "alpha changed"
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--legacy", required=True, type=Path)
    parser.add_argument("--dds", required=True, type=Path)
    parser.add_argument("--ddstools", required=True, type=Path)
    parser.add_argument("--retoc", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "assets/DarkPack")
    args = parser.parse_args()
    tint = style_float("detailsPage")
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        stage = work / "stage"
        for asset, recipe in TEXTURES.items():
            name = Path(asset).name
            source = args.legacy / "OblivionRemastered/Content" / asset
            target = stage / "OblivionRemastered/Content" / asset
            target.parent.mkdir(parents=True, exist_ok=True)
            for ext in (".uasset", ".uexp"):
                shutil.copy2(source.with_suffix(ext), target.with_suffix(ext))
            dds = recolour(args.dds / f"{name}.dds", tint, work)
            injected = work / "injected"
            subprocess.run([sys.executable, str(args.ddstools), str(target.with_suffix(".uasset")), str(dds), "--mode", "inject",
                            "--save_folder", str(injected), "--version", "5.3"], check=True)
            for ext in (".uasset", ".uexp"):
                made = next(injected.rglob(name + ext))
                shutil.copy2(made, target.with_suffix(ext))
            print(f"{name}: {recipe}, stock colour x {tint} (linear), stock alpha kept")
        args.out.mkdir(parents=True, exist_ok=True)
        utoc = args.out / f"{PACK}.utoc"
        subprocess.run([str(args.retoc), "to-zen", "--version", "UE5_3", str(stage), str(utoc)], check=True)
    for ext in ("pak", "utoc", "ucas"):
        path = args.out / f"{PACK}.{ext}"
        print(path.name, path.stat().st_size if path.exists() else "MISSING")


if __name__ == "__main__":
    main()
