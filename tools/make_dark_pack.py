"""Build the Dark UI pack (assets/DarkPack/MRDark_P.pak/.utoc/.ucas): stock textures re-coloured from src/core/DarkStyle.h.

Usage:
  python tools/make_dark_pack.py --paks <game>/OblivionRemastered/Content/Paks --ddstools <UE4-DDS-Tools/src/main.py>
                                 --retoc <retoc.exe> [--out <dir>] [--check]

Steps per texture: retoc to-legacy (filtered to the texture; retoc reads only the base containers, never ~mods), UE4-DDS-Tools
export to DDS, the recipe below re-colours it, UE4-DDS-Tools inject; then one retoc to-zen for the whole pack. Writes
MRDark_P.json next to the pack: the DarkStyle.h values and stock inputs it was built from and the hashes of the files it wrote
(tests/dark_pack_tests.py fails when DarkStyle.h changed without a rebuild). --check builds into a temp folder and compares.

Run it again after changing a DarkStyle.h value a recipe reads. The plugin copies the pack into Content/Paks/~mods when the
preset is Dark (src/core/rules/DarkPackSwitch.h). Requires Pillow. Plan: KB mods/menus-revamped/evidence/dark-ui-pack-plan.md.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PACK = "MRDark_P"
GAME_BUILD = "1.512.105.0"
# stock asset (path under Content) -> recipe name
TEXTURES = {
    # the parchment shared by the Inventory and Magic details, Help, Spell Purchase, Stats page 3 and Quest details
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


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def header_size(data):
    assert data[:4] == b"DDS "
    if data[84:88] == b"DXT5":
        return 128
    assert data[84:88] == b"DX10" and int.from_bytes(data[128:132], "little") in (77, 78), "not BC3"
    return 148


def tint_rgb_keep_alpha(stock_dds, tint, work):
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
    out = work / "recoloured" / stock_dds.name
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(stock[:header_size(stock)] + blocks)
    back = Image.open(out).convert("RGBA")
    assert back.getchannel("A").tobytes() == image.getchannel("A").tobytes(), "alpha changed"
    return out


# recipe -> (DarkStyle.h floats it reads, function(stock dds, values, work) -> recoloured dds)
RECIPES = {
    # what the Inventory details showed at run time: stock colour x detailsPage (linear multiply)
    "details_page": (("detailsPage",), lambda dds, v, work: tint_rgb_keep_alpha(dds, v["detailsPage"], work)),
}


def run(cmd):
    result = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors="replace")
    if result.returncode:
        sys.exit(f"failed ({result.returncode}): {' '.join(map(str, cmd))}\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result.stdout


def build(args, out):
    style = {}
    textures = {}
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        stage = work / "stage"
        for asset, recipe in TEXTURES.items():
            name = Path(asset).name
            names, make = RECIPES[recipe]
            values = {n: style_float(n) for n in names}
            style.update(values)
            legacy = work / "legacy" / name
            run([args.retoc, "to-legacy", "--no-shaders", "--version", "UE5_3", "-f", name, args.paks, legacy])
            source = legacy / "OblivionRemastered/Content" / asset
            if not source.with_suffix(".uasset").is_file():
                sys.exit(f"retoc did not extract {asset} from {args.paks}")
            exported = work / "dds" / name
            run([sys.executable, args.ddstools, source.with_suffix(".uasset"), "--mode", "export", "--export_as", "dds",
                 "--save_folder", exported, "--version", "5.3"])
            stock_dds = next(exported.rglob(name + ".dds"))
            dds = make(stock_dds, values, work)
            target = stage / "OblivionRemastered/Content" / asset
            target.parent.mkdir(parents=True, exist_ok=True)
            for ext in (".uasset", ".uexp"):
                shutil.copy2(source.with_suffix(ext), target.with_suffix(ext))
            injected = work / "injected" / name
            run([sys.executable, args.ddstools, target.with_suffix(".uasset"), dds, "--mode", "inject",
                 "--save_folder", injected, "--version", "5.3"])
            for ext in (".uasset", ".uexp"):
                shutil.copy2(next(injected.rglob(name + ext)), target.with_suffix(ext))
            textures[asset] = {"recipe": recipe, "style": sorted(values),
                               "stock_uexp_sha256": sha256(source.with_suffix(".uexp"))}
            print(f"{name}: {recipe} {values}")
        out.mkdir(parents=True, exist_ok=True)
        run([args.retoc, "to-zen", "--version", "UE5_3", stage, out / f"{PACK}.utoc"])
    files = {}
    for ext in ("pak", "utoc", "ucas"):
        path = out / f"{PACK}.{ext}"
        if not path.is_file():
            sys.exit(f"retoc wrote no {path.name}")
        files[path.name] = sha256(path)
    manifest = {"pack": PACK, "game_build": GAME_BUILD, "built_by": "tools/make_dark_pack.py",
                "style": style, "textures": textures, "files": files}
    (out / f"{PACK}.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--paks", required=True, type=Path, help="the game's OblivionRemastered/Content/Paks")
    parser.add_argument("--ddstools", required=True, type=Path)
    parser.add_argument("--retoc", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "assets/DarkPack")
    parser.add_argument("--check", action="store_true", help="build into a temp folder and compare with --out")
    args = parser.parse_args()
    if not args.check:
        manifest = build(args, args.out)
        for name, digest in manifest["files"].items():
            print(name, (args.out / name).stat().st_size, digest[:16])
        return
    with tempfile.TemporaryDirectory() as temp:
        fresh = build(args, Path(temp))
    current = json.loads((args.out / f"{PACK}.json").read_text(encoding="utf-8"))
    same = fresh == current and all(sha256(args.out / n) == d for n, d in fresh["files"].items())
    print("pack is up to date (rebuild is byte-identical)" if same else "pack is STALE: run without --check to rebuild")
    sys.exit(0 if same else 1)


if __name__ == "__main__":
    main()
