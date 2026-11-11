"""Build the colour pack (default assets/DarkPack/MRDark_P.pak/.utoc/.ucas): stock textures re-coloured from the colour file.

Usage:
  python tools/make_dark_pack.py --paks <game>/OblivionRemastered/Content/Paks --ddstools <UE4-DDS-Tools/src/main.py>
                                 --retoc <retoc.exe> [--out <dir>] [--pack <name>_P] [--colours <ini>] [--check]

What is baked lives in data files, one per menu (tools/menus/<menu>.json: asset -> recipe -> colour elements, stock checks,
shared-with notes); the recipes are the code below. The colours come from CustomColours.ini (default config/CustomColours.ini,
generated from src/core/DarkStyle.h by tools/custom_colours.py; a missing file or entry = today's Dark value).

Steps per texture: retoc to-legacy (filtered to the texture; retoc reads only the base containers, never ~mods), UE4-DDS-Tools
export to DDS, the recipe re-colours it, UE4-DDS-Tools inject; then one retoc to-zen for the whole pack. Writes <pack>.json
next to the pack: the colour values and stock inputs it was built from and the hashes of the files it wrote
(tests/dark_pack_tests.py fails when DarkStyle.h changed without a rebuild). --check builds into a temp folder and compares the
baked textures (retoc orders a many-package container differently per run, so the container bytes can differ).

Every recipe keeps the stock BC3 alpha blocks byte for byte, except enchanted_dot (it draws its own round alpha) and
magic_tab (the tab strip is made opaque). rich_row patches one colour of a rich-text style table (a DataTable .uexp).
Run it again after changing a colour a recipe reads. The plugin copies MRDark_P into Content/Paks/~mods when the
preset is Dark (src/core/rules/DarkPackSwitch.h). Requires Pillow. Plans: KB mods/menus-revamped/evidence/dark-ui-pack-plan.md,
custom-pack-builder-plan.md.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import custom_colours  # noqa: E402

DEFAULT_PACK = "MRDark_P"   # MRCustom_P comes with the plugin switch (Custom pack builder Phase 3)
GAME_BUILD = "1.512.105.0"
MENUS = ROOT / "tools/menus"


def load_menus(folder=MENUS):
    """The pack's tables from the per-menu data files (<menu>.json, in their "order"):
    TEXTURES {stock asset (path under Content): recipe name}, GLYPH_MASKS {recipe kind: [mask texture names]},
    RICH_ROWS {table: {row: (.uexp offset of its linear float4, stock value)}},
    BLUEPRINT_COLOURS {package: [(.uexp offset of a linear RGBA float4, stock value, colour element)]}."""
    menus = []
    for path in Path(folder).glob("*.json"):
        try:
            menus.append(json.loads(path.read_text(encoding="utf-8")))
        except ValueError as error:
            sys.exit(f"{path.name}: not valid JSON ({error})")
    textures, masks, rich_rows, blueprints = {}, {}, {}, {}
    for menu in sorted(menus, key=lambda m: (m.get("order", 99), m.get("menu", ""))):
        for row in menu.get("assets", []):
            if row["asset"] in textures:
                sys.exit(f"{row['asset']} is listed twice in {folder}")
            textures[row["asset"]] = row["recipe"]
        masks |= {kind: tuple(names) for kind, names in menu.get("glyph_masks", {}).items()}
        for table, rows in menu.get("rich_rows", {}).items():
            rich_rows[table] = {row: (e["offset"], tuple(e["stock"])) for row, e in rows.items()}
        for package, entries in menu.get("blueprint_colours", {}).items():
            blueprints[package] = [(e["offset"], tuple(e["stock"]), e["colour"]) for e in entries]
    return textures, masks, rich_rows, blueprints


# the stock assets and their recipes ("flat:<element>" = that colour on the stock alpha), the tab glyph masks (the clean
# standalone category masks, in tab order), the rich-text row and blueprint colour literals with their stock values (game
# 1.512.105.0; KB dark-mode-ui.md and docs/alchemy/internals.md): all in tools/menus/<menu>.json
TEXTURES, GLYPH_MASKS, RICH_ROWS, BLUEPRINT_COLOURS = load_menus()
TAB_MASKS, MAGIC_TAB_MASKS = GLYPH_MASKS.get("tab", ()), GLYPH_MASKS.get("magic_tab", ())


def pack_name(name):
    """A pack name for ~mods: letters and digits with the _P suffix (the game mounts *_P paks after the base ones)."""
    if not re.fullmatch(r"[A-Za-z0-9]+_P", name or ""):
        sys.exit(f"pack name {name!r}: letters and digits ending in _P (for example MRDark_P)")
    return name


TAB_STYLE = ("selected", "unselected", "gap", "outline", "glyph", "glyphDim")
PANEL_DARK, PANEL_LIGHT = 42, 116   # stock brightness of the panel curve's ends (the installed pack's rule)


def linear(v):
    c = v / 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def srgb(l):
    l = max(0.0, min(1.0, l))
    c = l * 12.92 if l <= 0.0031308 else 1.055 * l ** (1 / 2.4) - 0.055
    return round(c * 255)


def lum(p):
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def header_size(data):
    assert data[:4] == b"DDS "
    if data[84:88] == b"DXT5":
        return 128
    assert data[84:88] == b"DX10" and int.from_bytes(data[128:132], "little") in (77, 78), "not BC3"
    return 148


def rgb565(rgb):
    r, g, b = (round(c) for c in rgb)
    return (round(r * 31 / 255) << 11) | (round(g * 63 / 255) << 5) | round(b * 31 / 255)


def flat_blocks(data, rgb):
    """Every BC3 colour block = one RGB565 colour (index 0); the alpha blocks are kept byte for byte."""
    out = bytearray(data)
    colour = rgb565(rgb).to_bytes(2, "little")
    for offset in range(header_size(data), len(out), 16):
        out[offset + 8:offset + 16] = colour + colour + bytes(4)
    return bytes(out)


def endpoint_curve(data, gray):
    """Each BC3 colour endpoint mapped to gray(its brightness); indices and alpha blocks kept (the installed panel's method)."""
    out = bytearray(data)
    for offset in range(header_size(data) + 8, len(out), 16):
        for at in (offset, offset + 2):
            e = int.from_bytes(out[at:at + 2], "little")
            r, g, b = ((e >> 11) & 31) * 255 / 31, ((e >> 5) & 63) * 255 / 63, (e & 31) * 255 / 31
            v = gray(lum((r, g, b)))
            out[at:at + 2] = rgb565((v, v, v)).to_bytes(2, "little")
    return bytes(out)


def gray565(v):
    """A 565 gray whose green is exactly twice its red/blue level: no green cast (rgb565 of a gray rounds green apart)."""
    k = max(0, min(31, round(v * 31 / 255)))
    return (k << 11) | ((2 * k) << 5) | k


def parchment_curve(v, low, high, dark, light):
    """Stock brightness low..high -> dark..light (clamped): the parchment's grain kept in a narrow neutral gray band."""
    t = min(1.0, max(0.0, (v - low) / (high - low))) if high > low else 0.5
    return dark + (light - dark) * t


def parchment(stock_dds, dark, light):
    """Every BC3 colour endpoint mapped to a neutral gray by the parchment curve over the texture's own brightness range
    (opaque pixels, 2nd..98th percentile); indices and alpha blocks kept, so the grain and torn edges stay."""
    image = Image.open(stock_dds).convert("RGBA")
    values = sorted(round(lum(p)) for p in image.get_flattened_data() if p[3] > 128)
    low, high = values[len(values) * 2 // 100], values[len(values) * 98 // 100]
    data = stock_dds.read_bytes()
    out = bytearray(data)
    for offset in range(header_size(data) + 8, len(out), 16):
        for at in (offset, offset + 2):
            e = int.from_bytes(out[at:at + 2], "little")
            r, g, b = ((e >> 11) & 31) * 255 / 31, ((e >> 5) & 63) * 255 / 63, (e & 31) * 255 / 31
            out[at:at + 2] = gray565(parchment_curve(lum((r, g, b)), low, high, dark, light)).to_bytes(2, "little")
    return bytes(out)


def panel_gray(v, frame=None, panel=None):
    """List page tone curve: stock brightness <= 42 -> frame, >= 116 -> panel, linear between (default: the Dark values)."""
    frame = custom_colours.DEFAULTS["frame"][0] if frame is None else frame
    panel = custom_colours.DEFAULTS["panel"][0] if panel is None else panel
    return frame + round((panel - frame) * min(1.0, max(0.0, (v - PANEL_DARK) / (PANEL_LIGHT - PANEL_DARK))))


def enchanted_dot(data, rgb):
    """A round dot (coverage radius 14.5 px around the centre) in one colour; replaces the stock art and its alpha."""
    out = bytearray(data)
    height = int.from_bytes(data[12:16], "little")
    width = int.from_bytes(data[16:20], "little")
    levels = (255, 0, 218, 182, 145, 109, 72, 36)   # BC3 alpha block with endpoints 255 and 0
    colour = rgb565(rgb).to_bytes(2, "little")
    cx, cy = width / 2 - 0.5, height / 2 - 0.5
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            offset = header_size(data) + ((y // 4) * (width // 4) + x // 4) * 16
            bits = 0
            for dy in range(4):
                for dx in range(4):
                    coverage = max(0.0, min(1.0, 14.5 - ((x + dx - cx) ** 2 + (y + dy - cy) ** 2) ** 0.5)) * 255
                    best = min(range(8), key=lambda j: (abs(levels[j] - coverage), j))
                    bits |= best << (3 * (dy * 4 + dx))
            out[offset:offset + 2] = bytes((255, 0))
            out[offset + 2:offset + 8] = bits.to_bytes(6, "little")
            out[offset + 8:offset + 16] = colour + colour + bytes(4)
    return bytes(out)


def splice_colour(stock_dds, rgba, work):
    """PIL encodes the colour, then only its colour blocks go into the stock texture: the stock alpha stays byte for byte."""
    colour_dds = work / "colour.dds"
    rgba.convert("RGBA").save(colour_dds, pixel_format="DXT5")
    stock = stock_dds.read_bytes()
    colour = colour_dds.read_bytes()
    blocks = bytearray(stock[header_size(stock):])
    replacement = colour[header_size(colour):]
    assert len(blocks) == len(replacement), "size mismatch"
    for offset in range(0, len(blocks), 16):
        blocks[offset + 8:offset + 16] = replacement[offset + 8:offset + 16]
    return stock[:header_size(stock)] + bytes(blocks)


def per_pixel(stock_dds, work, fn):
    image = Image.open(stock_dds).convert("RGBA")
    out = Image.new("RGBA", image.size)
    out.putdata([(*fn(p), 255) for p in image.get_flattened_data()])
    return splice_colour(stock_dds, out, work)


def tint_rgb_keep_alpha(stock_dds, tint, work):
    """RGB = stock colour times tint (linear); the stock BC3 alpha blocks are kept byte for byte."""
    table = [srgb(linear(v) * tint) for v in range(256)]
    return per_pixel(stock_dds, work, lambda p: (table[p[0]], table[p[1]], table[p[2]]))


def tab_atlas(stock_dds, active, v, work, stock):
    """The Inventory tab compiler (RMI-069/070, ported in make_dark_tab_art.py) with the stock alpha everywhere."""
    sys.path.insert(0, str(ROOT / "tools"))
    import make_dark_tab_art as tabs
    masks = [Image.open(stock(name)).convert("RGBA").getchannel("A") for name in TAB_MASKS]
    image = Image.open(stock_dds).convert("RGBA")
    return splice_colour(stock_dds, tabs.dark_tab(active, image, masks, v, opaque_strip=False), work)


def opaque_rows(stock_dds, colour_dds, rows):
    """The stock texture with the encoded alpha blocks of colour_dds in the block rows that hold pixel rows < rows (mip 0)."""
    out = bytearray(stock_dds)
    width = int.from_bytes(stock_dds[16:20], "little")
    start, source = header_size(stock_dds), header_size(colour_dds)
    for block in range(((rows + 3) // 4) * (width // 4)):
        at = block * 16
        out[start + at:start + at + 8] = colour_dds[source + at:source + at + 8]
    return bytes(out)


def magic_tab_atlas(stock_dds, active, v, work, stock):
    """The same compiler with the Magic glyph masks; the strip is opaque (the stock Magic atlases have see-through wedges there:
    owner screenshot, a light streak on the selected tab), the stock alpha below it (the fade into the title band)."""
    sys.path.insert(0, str(ROOT / "tools"))
    import make_dark_tab_art as tabs
    masks = [Image.open(stock(name)).convert("RGBA").getchannel("A") for name in MAGIC_TAB_MASKS]
    image = Image.open(stock_dds).convert("RGBA")
    spliced = splice_colour(stock_dds, tabs.dark_tab(active, image, masks, v, opaque_strip=True), work)
    return opaque_rows(spliced, (work / "colour.dds").read_bytes(), tabs.STRIP + 4)


def rich_row_colour(uexp, row, rgb, table="DT_UI_MagicMenu"):
    """A RichTextStyleRow's colour set to an sRGB colour (linear floats, alpha 1); refuses an unexpected stock value."""
    offset, expected = RICH_ROWS[table][row]
    found = struct.unpack_from("<4f", uexp, offset) if len(uexp) >= offset + 16 else ()
    if len(found) != 4 or any(abs(a - b) > 5e-4 for a, b in zip(found, expected)):
        sys.exit(f"{table} row {row}: stock colour {found} at byte {offset}, expected {expected} (game update? re-scan the table)")
    out = bytearray(uexp)
    struct.pack_into("<4f", out, offset, *(linear(c) for c in rgb), 1.0)
    return bytes(out)


def tone_colour(v, peak, rgb):
    """A stock brightness v (0..peak) drawn in one colour: black at 0, the colour itself at the art's brightest pixel."""
    k = min(1.0, v / peak) if peak > 0 else 0.0
    return tuple(round(c * k) for c in rgb)


def tone(data, rgb):
    """The art's own shading in one colour: each BC3 colour endpoint becomes the colour times its brightness
    relative to the brightest endpoint (every mip level; indices and alpha blocks kept, so the relief stays)."""
    out = bytearray(data)
    starts = range(header_size(data) + 8, len(out), 16)

    def endpoint_lum(e):
        return lum((((e >> 11) & 31) * 255 / 31, ((e >> 5) & 63) * 255 / 63, (e & 31) * 255 / 31))

    peak = max((endpoint_lum(int.from_bytes(out[at:at + 2], "little")) for o in starts for at in (o, o + 2)), default=255)
    for offset in starts:
        for at in (offset, offset + 2):
            v = endpoint_lum(int.from_bytes(out[at:at + 2], "little"))
            out[at:at + 2] = rgb565(tone_colour(v, peak, rgb)).to_bytes(2, "little")
    return bytes(out)


ART = ROOT / "assets-src/DarkPack"   # the owner's edited textures, one PNG per game texture name (not shipped)


def mip_sizes(width, height, count):
    """The (width, height) of each mip level, largest first."""
    return [(max(1, width >> i), max(1, height >> i)) for i in range(max(1, count))]


def owner_png(stock_dds, name, work):
    """The owner's PNG for this texture at the stock size, every mip level BC3-encoded by Pillow; only the colour blocks go into
    the stock texture, so the stock alpha (the shape the game's material expects) stays byte for byte."""
    stock = stock_dds.read_bytes()
    height, width = int.from_bytes(stock[12:16], "little"), int.from_bytes(stock[16:20], "little")
    art = Image.open(ART / (name + ".png")).convert("RGBA")
    blocks = bytearray(stock[header_size(stock):])
    at = 0
    for size in mip_sizes(width, height, int.from_bytes(stock[28:32], "little")):
        level = work / "level.dds"
        art.resize(size, Image.LANCZOS).save(level, pixel_format="DXT5")
        encoded = level.read_bytes()[header_size(level.read_bytes()):]
        for offset in range(0, len(encoded), 16):
            blocks[at + offset + 8:at + offset + 16] = encoded[offset + 8:offset + 16]
        at += len(encoded)
    assert at == len(blocks), "size mismatch"
    return stock[:header_size(stock)] + bytes(blocks)


def bp_colour(uexp, package, values):
    """A blueprint's colour literals set to sRGB colours (linear floats, alpha 1); refuses an unexpected stock value."""
    out = bytearray(uexp)
    for offset, expected, name in BLUEPRINT_COLOURS[package]:
        found = struct.unpack_from("<4f", uexp, offset) if len(uexp) >= offset + 16 else ()
        if len(found) != 4 or any(abs(a - b) > 5e-4 for a, b in zip(found, expected)):
            sys.exit(f"{package}: stock colour {found} at byte {offset}, expected {expected} (game update? re-scan the blueprint)")
        struct.pack_into("<4f", out, offset, *(linear(c) for c in values[name]), 1.0)
    return bytes(out)


def preview_frame(stock_dds, v, work):
    """The stock brightness inverted between previewFrameLight (black) and previewFrameDark (white)."""
    dark, light = v["previewFrameDark"], v["previewFrameLight"]
    return per_pixel(stock_dds, work, lambda p: tuple(
        round(dark[i]) + (round(light[i]) - round(dark[i])) * (255 - round(lum(p))) // 255 for i in range(3)))


def recipe(name):
    """recipe name -> (number elements, colour elements, function(stock dds, values, work, stock) -> DDS bytes)"""
    kind, _, arg = name.partition(":")
    if kind == "flat":
        return (), (arg,), lambda dds, v, work, stock: flat_blocks(dds.read_bytes(), v[arg])
    if kind == "tone":
        return (), (arg,), lambda dds, v, work, stock: tone(dds.read_bytes(), v[arg])
    if kind == "bp_colour":   # a widget blueprint, not a texture: build() patches its .uexp (no DDS)
        names = tuple(sorted({name for _, _, name in BLUEPRINT_COLOURS[arg]}))
        return (), names, lambda uexp, v, work, stock: bp_colour(uexp, arg, v)
    if kind == "tab":
        return TAB_STYLE, (), lambda dds, v, work, stock: tab_atlas(dds, int(arg), v, work, stock)
    if kind == "magic_tab":
        return TAB_STYLE, (), lambda dds, v, work, stock: magic_tab_atlas(dds, int(arg), v, work, stock)
    if kind == "rich_row":   # a data table, not a texture: build() patches its .uexp (no DDS)
        row, _, colour = arg.partition(":")
        return (), (colour,), lambda uexp, v, work, stock: rich_row_colour(uexp, row, v[colour])
    return {
        # what the Inventory details showed at run time: stock colour x detailsPage (linear multiply)
        "details_page": (("detailsPage",), (), lambda dds, v, work, stock: tint_rgb_keep_alpha(dds, v["detailsPage"], work)),
        "panel": ((), ("frame", "panel"), lambda dds, v, work, stock: endpoint_curve(
            dds.read_bytes(), lambda b: panel_gray(b, v["frame"][0], v["panel"][0]))),
        "parchment": ((), ("parchmentDark", "parchmentLight"), lambda dds, v, work, stock: parchment(
            dds, v["parchmentDark"][0], v["parchmentLight"][0])),
        "png": ((), (), lambda dds, v, work, stock: owner_png(dds, dds.stem, work)),
        "enchanted_dot": ((), ("enchantedDot",), lambda dds, v, work, stock: enchanted_dot(dds.read_bytes(), v["enchantedDot"])),
        # the preview background: the stock brightness without its brown cast
        "gray": ((), (), lambda dds, v, work, stock: per_pixel(dds, work, lambda p: (round(lum(p)),) * 3)),
        "preview_frame": ((), ("previewFrameLight", "previewFrameDark"), lambda dds, v, work, stock: preview_frame(dds, v, work)),
    }[name]


def recipe_values(recipe_name, colours):
    """The colour-file values a recipe reads: {element: value}."""
    floats, rgbs, _ = recipe(recipe_name)
    return {n: colours[n] for n in (*floats, *rgbs)}


def run(cmd):
    result = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors="replace")
    if result.returncode:
        sys.exit(f"failed ({result.returncode}): {' '.join(map(str, cmd))}\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return result.stdout


def build(args, out, pack=DEFAULT_PACK, colours=None):
    """Bake every asset of the data files with the colour values (default: the Dark values) into <out>/<pack>.*"""
    pack = pack_name(pack)
    colours = custom_colours.DEFAULTS if colours is None else colours
    style = {}
    textures = {}
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        stage = work / "stage"
        sources = {}

        def extract(name):
            """retoc + UE4-DDS-Tools: the stock texture's legacy folder and its DDS export (once per name)"""
            if name not in sources:
                legacy = work / "legacy" / name
                run([args.retoc, "to-legacy", "--no-shaders", "--version", "UE5_3", "-f", name, args.paks, legacy])
                found = sorted(legacy.rglob(name + ".uasset"))
                if not found:
                    sys.exit(f"retoc did not extract {name} from {args.paks}")
                exported = work / "dds" / name
                run([sys.executable, args.ddstools, found[0], "--mode", "export", "--export_as", "dds",
                     "--save_folder", exported, "--version", "5.3"])
                sources[name] = (legacy, next(exported.rglob(name + ".dds")))
            return sources[name]

        for asset, recipe_name in TEXTURES.items():
            name = Path(asset).name
            make = recipe(recipe_name)[2]
            values = recipe_values(recipe_name, colours)
            style.update(values)
            table = recipe_name.startswith(("rich_row:", "bp_colour:"))
            source_root = work / "legacy" / name
            if table:
                run([args.retoc, "to-legacy", "--no-shaders", "--version", "UE5_3", "-f", name, args.paks, source_root])
            else:
                source_root = extract(name)[0]
            source = source_root / "OblivionRemastered/Content" / asset
            if not source.with_suffix(".uasset").is_file():
                sys.exit(f"retoc did not extract {asset} from {args.paks}")
            target = stage / "OblivionRemastered/Content" / asset
            target.parent.mkdir(parents=True, exist_ok=True)
            if table:
                shutil.copy2(source.with_suffix(".uasset"), target.with_suffix(".uasset"))
                target.with_suffix(".uexp").write_bytes(make(source.with_suffix(".uexp").read_bytes(), values, work, None))
                textures[asset] = {"recipe": recipe_name, "style": sorted(values),
                                   "stock_uexp_sha256": sha256(source.with_suffix(".uexp")),
                                   "baked_uexp_sha256": sha256(target.with_suffix(".uexp"))}
                print(f"{name}: {recipe_name} {values}")
                continue
            stock_dds = extract(name)[1]
            dds = work / "recoloured" / (name + ".dds")
            dds.parent.mkdir(parents=True, exist_ok=True)
            dds.write_bytes(make(stock_dds, values, work, lambda n: extract(n)[1]))
            for ext in (".uasset", ".uexp"):
                shutil.copy2(source.with_suffix(ext), target.with_suffix(ext))
            injected = work / "injected" / name
            run([sys.executable, args.ddstools, target.with_suffix(".uasset"), dds, "--mode", "inject",
                 "--save_folder", injected, "--version", "5.3"])
            for ext in (".uasset", ".uexp"):
                shutil.copy2(next(injected.rglob(name + ext)), target.with_suffix(ext))
            textures[asset] = {"recipe": recipe_name, "style": sorted(values),
                               "stock_uexp_sha256": sha256(source.with_suffix(".uexp")),
                               "baked_uexp_sha256": sha256(target.with_suffix(".uexp"))}
            print(f"{name}: {recipe_name} {values}")
        out.mkdir(parents=True, exist_ok=True)
        run([args.retoc, "to-zen", "--version", "UE5_3", stage, out / f"{pack}.utoc"])
    files = {}
    for ext in ("pak", "utoc", "ucas"):
        path = out / f"{pack}.{ext}"
        if not path.is_file():
            sys.exit(f"retoc wrote no {path.name}")
        files[path.name] = sha256(path)
    manifest = {"pack": pack, "game_build": GAME_BUILD, "built_by": "tools/make_dark_pack.py",
                "style": style, "textures": textures, "files": files}
    (out / f"{pack}.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--paks", required=True, type=Path, help="the game's OblivionRemastered/Content/Paks")
    parser.add_argument("--ddstools", required=True, type=Path)
    parser.add_argument("--retoc", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "assets/DarkPack")
    parser.add_argument("--pack", default=DEFAULT_PACK, help=f"pack file name (default {DEFAULT_PACK})")
    parser.add_argument("--colours", type=Path, default=custom_colours.DEFAULT_PATH,
                        help="the colour file (default config/CustomColours.ini; missing file or entry = the Dark colours)")
    parser.add_argument("--check", action="store_true", help="build into a temp folder and compare with --out")
    args = parser.parse_args()
    pack = pack_name(args.pack)
    colours = custom_colours.load(args.colours)
    if not args.check:
        manifest = build(args, args.out, pack, colours)
        for name, digest in manifest["files"].items():
            print(name, (args.out / name).stat().st_size, digest[:16])
        return
    with tempfile.TemporaryDirectory() as temp:
        fresh = build(args, Path(temp), pack, colours)
    current = json.loads((args.out / f"{pack}.json").read_text(encoding="utf-8"))
    # retoc 0.1.5 to-zen orders the chunks of a many-package container differently per run, so the container files are not
    # compared: every baked texture (and the colour values) must be the same, and the shipped files the ones the manifest
    # names.
    fresh.pop("files")
    files = current.pop("files", {})
    same = fresh == current and all((args.out / n).is_file() and sha256(args.out / n) == d for n, d in files.items())
    print("pack is up to date (same baked textures)" if same else "pack is STALE: run without --check to rebuild")
    sys.exit(0 if same else 1)


if __name__ == "__main__":
    main()
