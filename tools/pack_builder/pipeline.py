"""The Custom pack build: stock textures and blueprints re-coloured from the colour file into <pack>.pak/.utoc/.ucas + <pack>.json.

Called by pack_builder.app.build_pack (the one build of the CLI tools/make_dark_pack.py and of the builder window); this module
holds the recipes and the pipeline, app.py adds the temp folder, the read-back and the replace of the installed pack.

What is baked lives in data files, one per menu (tools/menus/<menu>.json: asset -> recipe -> colour elements, stock checks,
shared-with notes); the recipes are the code below. The colours come from CustomColours.ini (default config/CustomColours.ini,
generated from src/core/DarkStyle.h by tools/custom_colours.py; a missing file or entry = today's Dark value).

Steps: one retoc to-legacy call extracts every name the build needs (one -f per asset and glyph mask; retoc reads only the base
containers, never ~mods). Per texture, UE4-DDS-Tools (imported from its src folder, no process per texture) exports the DDS,
the recipe re-colours it, UE4-DDS-Tools injects it; then one retoc to-zen for the whole pack. retoc needs Oodle
(oo2core_9_win64.dll) and downloads it next to itself on its first run (internet). Writes <pack>.json
next to the pack: the colour values and stock inputs it was built from and the hashes of the files it wrote
(tests/dark_pack_tests.py fails when DarkStyle.h changed without a rebuild). --check builds into a temp folder and compares the
baked textures (retoc orders a many-package container differently per run, so the container bytes can differ).

Every recipe keeps the stock BC3 alpha blocks byte for byte, except enchanted_dot (it draws its own round alpha) and
magic_tab (the tab strip is made opaque). rich_row patches one colour of a rich-text style table (a DataTable .uexp).
Run it again after changing a colour a recipe reads. The plugin copies MRCustom_P into Content/Paks/~mods when the
preset is Custom (src/core/rules/DarkPackSwitch.h). Requires Pillow. Plans: KB mods/menus-revamped/evidence/dark-ui-pack-plan.md,
custom-pack-builder-plan.md.

Each asset is checked before it is written (Phase 4, design point 7): blueprint and rich-text stock colour literals, BC3 format,
the recipe's result the size of the stock texture, the stock .uexp hash for the laurel and alchemy recipes, the owner's art
present for the png recipe (development only: no data file row uses it since Phase 6). A failing asset is skipped and
listed in the manifest's "skipped" (the game's stock asset shows); the rest is built.
"""
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

TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
import custom_colours  # noqa: E402
import make_dark_tab_art as tabs  # noqa: E402
from pack_builder import VERSION, BuildError, paths  # noqa: E402

ROOT = TOOLS.parent
DEFAULT_PACK = "MRCustom_P"   # the plugin switch installs it for uPreset=2 (Custom pack builder Phase 3; was MRDark_P)
GAME_BUILD = "1.512.105.0"    # the game build the stock checks were made on (the exe's version resource holds no build)
MENUS = paths.defaults()["menus"]
LAUREL_STOCK_SHA256 = "62be812455818cd56e241058bd2093659a3e5d94e3eab616f35998f485cb89ef"
# the alchemy recipe's stock textures (.uexp SHA-256, game 1.512.105.0; KB docs/alchemy/internals.md)
ALCHEMY_STOCK_SHA256 = {
    "T_UI_Alchemy_Background": "10989a5b51e32941b2d8940adee7dc6251502535a2b5191c8486f936c38891a9",
    "T_Alchemy_IngredientsSlots_BG": "02e3d4bb82c118d5fa01cf69b4bdd1a3c876d48c02ca32b2ce46f894b069fa99",
    "T_Alchemy_mortar_pestle": "9643a8a439f01440da156b79b40ac4d77a909365dd6b11cbd55d922ffdbcb659",
}


class SkipAsset(Exception):
    """An asset that failed its check: skipped and listed, the rest of the pack is built."""


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
            raise BuildError(f"{path.name}: not valid JSON ({error})")
    textures, masks, rich_rows, blueprints = {}, {}, {}, {}
    for menu in sorted(menus, key=lambda m: (m.get("order", 99), m.get("menu", ""))):
        for row in menu.get("assets", []):
            if row["asset"] in textures:
                raise BuildError(f"{row['asset']} is listed twice in {folder}")
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
        raise BuildError(f"pack name {name!r}: letters and digits ending in _P (for example MRDark_P)")
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


def laurels(stock, rgb, tint):
    """Phase 4 flat laurel colour multiplied by the linear widget tint, including opacity."""
    if tuple(tint[:3]) != (1, 1, 1):
        base = rgb565(rgb)
        channels = ((base >> 11) & 31, (base >> 5) & 63, base & 31)
        rgb = [srgb(linear(c * 255 / levels) * factor)
               for c, levels, factor in zip(channels, (31, 63, 31), tint)]
    baked = bytearray(flat_blocks(stock, rgb))
    if tint[3] != 1:
        for offset in range(header_size(baked), len(baked), 16):
            baked[offset:offset + 8] = scale_bc3_alpha(baked[offset:offset + 8], tint[3])
    return bytes(baked)


def alpha_levels(first, second):
    if first > second:
        return [first, second] + [((7 - i) * first + i * second) // 7 for i in range(1, 7)]
    return [first, second] + [((5 - i) * first + i * second) // 5 for i in range(1, 5)] + [0, 255]


def scale_bc3_alpha(block, opacity):
    """Keep the scaled alpha extrema exactly, quantizing only the six intermediate levels."""
    scaled = [round(a * opacity) for a in alpha_levels(*block[:2])]
    bits = int.from_bytes(block[2:], "little")
    pixels = [scaled[(bits >> shift) & 7] for shift in range(0, 48, 3)]
    first, second = max(pixels), min(pixels)
    if first == second:
        return bytes((first, second)) + bytes(6)
    levels = alpha_levels(first, second)
    indices = {a: min(range(8), key=lambda i: abs(levels[i] - a)) for a in set(pixels)}
    result = sum(indices[a] << (3 * i) for i, a in enumerate(pixels))
    return bytes((first, second)) + result.to_bytes(6, "little")


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
    masks = [Image.open(stock(name)).convert("RGBA").getchannel("A") for name in MAGIC_TAB_MASKS]
    image = Image.open(stock_dds).convert("RGBA")
    spliced = splice_colour(stock_dds, tabs.dark_tab(active, image, masks, v, opaque_strip=True), work)
    return opaque_rows(spliced, (work / "colour.dds").read_bytes(), tabs.STRIP + 4)


def rich_row_colour(uexp, row, rgb, table="DT_UI_MagicMenu"):
    """A RichTextStyleRow's colour set to an sRGB colour (linear floats, alpha 1); refuses an unexpected stock value."""
    offset, expected = RICH_ROWS[table][row]
    found = struct.unpack_from("<4f", uexp, offset) if len(uexp) >= offset + 16 else ()
    if len(found) != 4 or any(abs(a - b) > 5e-4 for a, b in zip(found, expected)):
        raise SkipAsset(f"{table} row {row}: stock colour {found} at byte {offset}, expected {expected} (game update? re-scan the table)")
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


ALCHEMY_WARM = (15, 35)   # stock R - B (sRGB): <= 15 is stone, >= 35 is metal, blended between
ALCHEMY_CONTRAST = 0.8    # the stock relief kept a little softer (fitted to the owner art, KB plan page "Phase 6 results")


def alchemy(stock_dds, metal, stone):
    """The Alchemy frame, ingredient slots and pot: the stock bronze metal (warm pixels) recoloured to metal, the green-grey
    stone to stone. Each BC3 colour endpoint becomes its colour times (its linear brightness / that material's average
    brightness in this texture) ** ALCHEMY_CONTRAST, so each texture's metal and stone average their colours and keep the
    stock relief; indices and alpha blocks kept (like parchment and tone)."""
    low, high = ALCHEMY_WARM

    def weight(r, g, b):
        return min(1.0, max(0.0, ((r - b) - low) / (high - low)))

    def brightness(r, g, b):
        return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b)

    sums = [0.0, 0.0, 0.0, 0.0]   # metal brightness, metal weight, stone brightness, stone weight (opaque pixels)
    for n, (r, g, b, a) in Image.open(stock_dds).convert("RGBA").getcolors(1 << 24):
        if a > 128:
            w, v = weight(r, g, b), brightness(r, g, b)
            sums[0] += n * w * v
            sums[1] += n * w
            sums[2] += n * (1 - w) * v
            sums[3] += n * (1 - w)
    ref_metal = sums[0] / sums[1] if sums[1] and sums[0] else 1.0
    ref_stone = sums[2] / sums[3] if sums[3] and sums[2] else 1.0
    metal_lin, stone_lin = [linear(c) for c in metal], [linear(c) for c in stone]
    data = Path(stock_dds).read_bytes()
    out = bytearray(data)
    mapped = {}
    for offset in range(header_size(data) + 8, len(out), 16):
        for at in (offset, offset + 2):
            e = int.from_bytes(out[at:at + 2], "little")
            if e not in mapped:
                r, g, b = ((e >> 11) & 31) * 255 / 31, ((e >> 5) & 63) * 255 / 63, (e & 31) * 255 / 31
                w, v = weight(r, g, b), brightness(r, g, b)
                k_metal = (v / ref_metal) ** ALCHEMY_CONTRAST if v > 0 else 0.0
                k_stone = (v / ref_stone) ** ALCHEMY_CONTRAST if v > 0 else 0.0
                mapped[e] = rgb565([srgb(w * m * k_metal + (1 - w) * s * k_stone)
                                    for m, s in zip(metal_lin, stone_lin)]).to_bytes(2, "little")
            out[at:at + 2] = mapped[e]
    return bytes(out)


ART = paths.defaults()["art"]   # the owner's edited textures, one PNG per game texture name (not shipped; owner decision 9)


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
            raise SkipAsset(f"{package}: stock colour {found} at byte {offset}, expected {expected} (game update? re-scan the blueprint)")
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
        "laurels": ((), ("pattern", "Skin.pattern"), lambda dds, v, work, stock: laurels(
            dds.read_bytes(), v["pattern"], v["Skin.pattern"])),
        "details_page": (("detailsPage",), (), lambda dds, v, work, stock: tint_rgb_keep_alpha(dds, v["detailsPage"], work)),
        "panel": ((), ("frame", "panel"), lambda dds, v, work, stock: endpoint_curve(
            dds.read_bytes(), lambda b: panel_gray(b, v["frame"][0], v["panel"][0]))),
        "parchment": ((), ("parchmentDark", "parchmentLight"), lambda dds, v, work, stock: parchment(
            dds, v["parchmentDark"][0], v["parchmentLight"][0])),
        "alchemy": ((), ("alchemyMetal", "alchemyStone"), lambda dds, v, work, stock: alchemy(
            dds, v["alchemyMetal"], v["alchemyStone"])),
        "png": ((), (), lambda dds, v, work, stock: owner_png(dds, dds.stem, work)),   # development only (owner art)
        "enchanted_dot": ((), ("enchantedDot",), lambda dds, v, work, stock: enchanted_dot(dds.read_bytes(), v["enchantedDot"])),
        # the preview background: the stock brightness without its brown cast
        "gray": ((), (), lambda dds, v, work, stock: per_pixel(dds, work, lambda p: (round(lum(p)),) * 3)),
        "preview_frame": ((), ("previewFrameLight", "previewFrameDark"), lambda dds, v, work, stock: preview_frame(dds, v, work)),
    }[name]


def recipe_values(recipe_name, colours):
    """The colour-file values a recipe reads: {element: value}."""
    floats, rgbs, _ = recipe(recipe_name)
    return {n: colours[n] for n in (*floats, *rgbs)}


OODLE_DLL = paths.OODLE_DLL
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)   # the windowed builder must not flash a console per retoc call


def run(cmd):
    cmd = [str(c) for c in cmd]
    result = subprocess.run(cmd, capture_output=True, text=True, errors="replace", creationflags=NO_WINDOW)
    if result.returncode:
        hint = ""
        retoc = Path(cmd[0])
        if retoc.stem.lower() == "retoc" and not (retoc.parent / OODLE_DLL).is_file():
            hint = (f"\nretoc needs Oodle ({OODLE_DLL}) to read the game's containers. It downloads the DLL next to itself on its "
                    f"first run, which needs internet and a writable folder. Connect to the internet and run again, or copy an "
                    f"{OODLE_DLL} next to {retoc}.")
        raise BuildError(f"failed ({result.returncode}): {' '.join(cmd)}\n{result.stdout[-2000:]}{result.stderr[-2000:]}{hint}")
    return result.stdout


def dds_tools(path=None):
    """UE4-DDS-Tools in-process: (export(uasset, dds), inject(uasset, dds, out_uasset)). path: its src/main.py or its src
    folder; None = the copy frozen into the builder exe (or already importable)."""
    if path is not None:
        src = Path(path)
        src = src.parent if src.suffix == ".py" else src
        if not (src / "unreal/uasset.py").is_file() or not (src / "directx/dds.py").is_file():
            raise BuildError(f"UE4-DDS-Tools not found in {src} (--ddstools: its src/main.py or its src folder)")
        if str(src) not in sys.path:
            sys.path.insert(0, str(src))
    try:
        from unreal.uasset import Uasset
        from directx.dds import DDS
    except Exception as error:
        where = f"in {src}" if path is not None else "(--ddstools: its src/main.py or its src folder)"
        raise BuildError(f"UE4-DDS-Tools {where} did not load ({type(error).__name__}: {error})")

    def texture(asset, uasset):
        found = asset.get_texture_list()
        if len(found) != 1:
            raise SkipAsset(f"{Path(uasset).name}: expected one texture, found {len(found)}")
        return found[0]

    def export(uasset, dds):
        tex = texture(Uasset(str(uasset), version="5.3"), uasset)
        Path(dds).parent.mkdir(parents=True, exist_ok=True)
        tex.get_dds().save(str(dds))

    def inject(uasset, dds, out_uasset):
        asset = Uasset(str(uasset), version="5.3")
        texture(asset, uasset).inject_dds(DDS.load(str(dds)))
        asset.update_package_source(is_official=False)
        Path(out_uasset).parent.mkdir(parents=True, exist_ok=True)
        asset.save(str(out_uasset))

    return export, inject


def needed_names():
    """Every name the build extracts: the data files' assets and the glyph masks."""
    return sorted({Path(a).name for a in TEXTURES} | {n for names in GLYPH_MASKS.values() for n in names})


def extract_all(args, folder):
    """One retoc to-legacy call with one -f per needed name into folder; refuses any asset it did not extract (a wrong folder
    or a game update: nothing is built). Returns {lower-case name: sorted .uasset paths}."""
    cmd = [args.retoc, "to-legacy", "--no-shaders", "--version", "UE5_3"]
    for name in needed_names():
        cmd += ["-f", name]
    run(cmd + [args.paks, folder])
    found = {}
    for path in sorted(Path(folder).rglob("*.uasset")):
        found.setdefault(path.stem.lower(), []).append(path)
    content = Path(folder) / "OblivionRemastered/Content"
    missing = [a for a in TEXTURES if not (content / a).with_suffix(".uasset").is_file()]
    missing += [n for names in GLYPH_MASKS.values() for n in names if n.lower() not in found]
    if missing:
        raise BuildError(f"retoc did not extract {len(missing)} asset(s) from {args.paks} (game update or wrong folder?):\n  "
                         + "\n  ".join(missing))
    return found


def checked_texture(stock, baked):
    """Refuses a recipe result the game would not take: not the stock size or header (dimensions, mips, format)."""
    if len(baked) != len(stock):
        raise SkipAsset(f"the recipe's result has another size ({len(baked)} bytes, stock texture {len(stock)})")
    head = header_size(stock)
    if baked[:head] != stock[:head]:
        raise SkipAsset("the recipe changed the texture header (size, mips or format)")
    return baked


def bake(asset, recipe_name, colours, art, legacy, stage, work, stock_dds, inject):
    """One asset into the stage folder: (values it read, manifest entry). Raises SkipAsset before writing when a check fails."""
    name = Path(asset).name
    kind = recipe_name.partition(":")[0]
    make = recipe(recipe_name)[2]
    values = recipe_values(recipe_name, colours)
    source = legacy / "OblivionRemastered/Content" / asset
    target = stage / "OblivionRemastered/Content" / asset
    if kind == "laurels" and sha256(source.with_suffix(".uexp")) != LAUREL_STOCK_SHA256:
        raise SkipAsset("laurel stock asset differs from the verified game build")
    if kind == "alchemy" and sha256(source.with_suffix(".uexp")) != ALCHEMY_STOCK_SHA256.get(name):
        raise SkipAsset(f"{name}: stock asset differs from the verified game build (game update?)")
    if kind == "png" and not (art / (name + ".png")).is_file():
        raise SkipAsset(f"owner art missing ({name}.png in {art}); the game's stock texture shows")
    if kind in ("rich_row", "bp_colour"):   # a data table or a widget blueprint: its .uexp is patched (no DDS)
        uexp = make(source.with_suffix(".uexp").read_bytes(), values, work, None)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source.with_suffix(".uasset"), target.with_suffix(".uasset"))
        target.with_suffix(".uexp").write_bytes(uexp)
    else:
        stock_path = stock_dds(name)
        stock = stock_path.read_bytes()
        try:
            header_size(stock)
        except AssertionError:
            raise SkipAsset("the stock texture is not BC3 (game update?)") from None
        dds = work / "recoloured" / (name + ".dds")
        dds.parent.mkdir(parents=True, exist_ok=True)
        dds.write_bytes(checked_texture(stock, make(stock_path, values, work, stock_dds)))
        target.parent.mkdir(parents=True, exist_ok=True)
        for ext in (".uasset", ".uexp"):
            shutil.copy2(source.with_suffix(ext), target.with_suffix(ext))
        injected = work / "injected" / (name + ".uasset")
        inject(target.with_suffix(".uasset"), dds, injected)
        for ext in (".uasset", ".uexp"):
            shutil.copy2(injected.with_suffix(ext), target.with_suffix(ext))
    return values, {"recipe": recipe_name, "style": sorted(values),
                    "stock_uexp_sha256": sha256(source.with_suffix(".uexp")),
                    "baked_uexp_sha256": sha256(target.with_suffix(".uexp"))}


def build(args, out, pack=DEFAULT_PACK, colours=None, art=None, log=print):
    """Bake every asset of the data files with the colour values (default: the Dark values) into <out>/<pack>.*; an asset that
    fails its check is skipped and listed. args: paks, retoc, ddstools (None = the frozen copy). Returns the manifest."""
    global ART
    saved, ART = ART, Path(ART if art is None else art)   # the png recipe (owner_png) reads ART
    try:
        return _build(args, out, pack, colours, ART, log)
    finally:
        ART = saved


def _build(args, out, pack, colours, art, log):
    pack = pack_name(pack)
    colours = custom_colours.pack_values() | (colours or {})
    log = log or (lambda line: None)
    style, textures, skipped = {}, {}, []
    if not Path(args.retoc).is_file():
        raise BuildError(f"retoc not found: {args.retoc} (--retoc: the path of retoc.exe)")
    export, inject = dds_tools(getattr(args, "ddstools", None))
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        stage = work / "stage"
        legacy = work / "legacy"
        log(f"Reading {len(needed_names())} stock assets from {args.paks}")
        found = extract_all(args, legacy)
        exported = {}

        def stock_dds(name):
            """The stock texture's DDS export (once per name; the first extracted file of that name, as before)"""
            if name not in exported:
                uasset = found[name.lower()][0]
                exported[name] = work / "dds" / (uasset.stem + ".dds")
                export(uasset, exported[name])
            return exported[name]

        for asset, recipe_name in TEXTURES.items():
            name = Path(asset).name
            target = stage / "OblivionRemastered/Content" / asset
            try:
                values, entry = bake(asset, recipe_name, colours, art, legacy, stage, work, stock_dds, inject)
            except (BuildError, KeyboardInterrupt):
                raise
            except Exception as error:   # SkipAsset or a recipe failing on unexpected data: skip and list, build the rest
                reason = str(error) if isinstance(error, SkipAsset) else f"{type(error).__name__}: {error}"
                for ext in (".uasset", ".uexp"):
                    target.with_suffix(ext).unlink(missing_ok=True)
                skipped.append({"asset": asset, "recipe": recipe_name, "reason": reason})
                log(f"SKIPPED {name}: {reason}")
                continue
            style.update(values)
            textures[asset] = entry
            log(f"{name}: {recipe_name} {values}")
        if not textures:
            raise BuildError(f"no asset passed its check ({len(skipped)} skipped): nothing to build")
        out.mkdir(parents=True, exist_ok=True)
        log("Writing the pack")
        run([args.retoc, "to-zen", "--version", "UE5_3", stage, out / f"{pack}.utoc"])
    files = {}
    for ext in ("pak", "utoc", "ucas"):
        path = out / f"{pack}.{ext}"
        if not path.is_file():
            raise BuildError(f"retoc wrote no {path.name}")
        files[path.name] = sha256(path)
    manifest = {"pack": pack, "game_build": GAME_BUILD,
                "built_by": paths.EXE_NAME if paths.is_frozen() else "tools/make_dark_pack.py",
                "builder_version": VERSION, "style": style, "textures": textures, "skipped": skipped, "files": files}
    (out / f"{pack}.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def readback(retoc, utoc, assets, log=print):
    """retoc reads the new pack back: the container verifies and lists every built package (before anything is replaced)."""
    log = log or (lambda line: None)
    if "verified" not in run([retoc, "verify", utoc]).lower():
        raise BuildError(f"read-back: retoc could not verify {Path(utoc).name}; the installed pack was not changed")
    listed = {line.split()[-1].lower() for line in run([retoc, "list", "--path", utoc]).splitlines()
              if "ExportBundleData" in line}
    missing = [a for a in assets if f"../../../oblivionremastered/content/{a}.uasset".lower() not in listed]
    if missing:
        raise BuildError(f"read-back: the new pack lacks {len(missing)} package(s): "
                         + ", ".join(Path(a).name for a in missing) + "; the installed pack was not changed")
    log(f"Read back: {len(listed)} packages verified")
