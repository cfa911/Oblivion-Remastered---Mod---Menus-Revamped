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
import gc
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
from pack_builder import charcoal_tabs  # noqa: E402
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


# package -> [{from_package, from_object, from_class, to_package, to_object, to_class}]: imports a package's .uasset is rewritten
# to (RMI-096: the popups draw T_UI_BackgroundHorizontal instead of the paper material), filled by load_menus
IMPORT_SWAPS = {}
BLUEPRINT_SCALES = {}   # {(package, .uexp offset): factor on its sRGB colour value} (RMI-091: the container lines, pattern x 1.6)


def load_menus(folder=MENUS):
    """The pack's tables from the per-menu data files (<menu>.json, in their "order"):
    TEXTURES {stock asset (path under Content): recipe name}, GLYPH_MASKS {recipe kind: [mask texture names]},
    RICH_ROWS {table: {row: (.uexp offset of its linear float4, stock value)}},
    BLUEPRINT_COLOURS {package: [(.uexp offset of a linear RGBA float4, stock value, colour element)]}: the widget blueprints'
    "blueprint_colours" and the materials' "material_colours" (the same in-place literal patch)."""
    menus = []
    for path in Path(folder).glob("*.json"):
        try:
            menus.append(json.loads(path.read_text(encoding="utf-8")))
        except ValueError as error:
            raise BuildError(f"{path.name}: not valid JSON ({error})")
    textures, masks, rich_rows, blueprints = {}, {}, {}, {}
    IMPORT_SWAPS.clear()
    BLUEPRINT_SCALES.clear()
    for menu in sorted(menus, key=lambda m: (m.get("order", 99), m.get("menu", ""))):
        for row in menu.get("assets", []):
            if row["asset"] in textures:
                raise BuildError(f"{row['asset']} is listed twice in {folder}")
            textures[row["asset"]] = row["recipe"]
        masks |= {kind: tuple(names) for kind, names in menu.get("glyph_masks", {}).items()}
        for table, rows in menu.get("rich_rows", {}).items():
            rich_rows[table] = {row: (e["offset"], tuple(e["stock"])) for row, e in rows.items()}
        IMPORT_SWAPS.update(menu.get("import_swaps", {}))
        for section in ("blueprint_colours", "material_colours"):
            for package, entries in menu.get(section, {}).items():
                blueprints[package] = [(e["offset"], tuple(e["stock"]), e["colour"]) for e in entries]
                BLUEPRINT_SCALES.update({(package, e["offset"]): float(e["scale"]) for e in entries if "scale" in e})
    return textures, masks, rich_rows, blueprints


# the stock assets and their recipes ("flat:<element>" = that colour on the stock alpha), the tab glyph masks (the clean
# standalone category masks, in tab order), the rich-text row and blueprint colour literals with their stock values (game
# 1.512.105.0; KB dark-mode-ui.md and docs/alchemy/internals.md): all in tools/menus/<menu>.json
TEXTURES, GLYPH_MASKS, RICH_ROWS, BLUEPRINT_COLOURS = load_menus()
TAB_MASKS, MAGIC_TAB_MASKS = GLYPH_MASKS.get("tab", ()), GLYPH_MASKS.get("magic_tab", ())
LAYOUT_PACK = "MRLayout_P"   # sizes only (Step B, decision 46): installed by the plugin when MRCustom_P is not


def load_layout(folder=MENUS):
    """{package: {"asset", "floats": [{widget, offset, stock, size|value, type?}], "bytes": [{widget, offset, stock, value}],
    "rewrites": [...]}} from the data files'
    "layout" sections (tools/menus/layout.json: today's compact rows, Step B decisions 29/45)."""
    layout = {}
    for path in Path(folder).glob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except ValueError as error:
            raise BuildError(f"{path.name}: not valid JSON ({error})")
        for package, row in data.get("layout", {}).items():
            if package in layout:
                raise BuildError(f"layout package {package} is listed twice in {folder}")
            layout[package] = row
    return layout


LAYOUT = load_layout()


def load_literals(folder=MENUS):
    """{package: [{offset, type: float|double, stock, value, widget}]} from the data files' "blueprint_literals": fixed
    non-colour literals of a colour-patched blueprint, baked in the colour pack only (RMI-089: the Inventory details page
    drop shadow UseShadow? 1.0 -> 0.0; Classic/Light and MRLayout_P keep the stock value)."""
    literals = {}
    for path in Path(folder).glob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except ValueError as error:
            raise BuildError(f"{path.name}: not valid JSON ({error})")
        for package, rows in data.get("blueprint_literals", {}).items():
            if package in literals:
                raise BuildError(f"blueprint_literals package {package} is listed twice in {folder}")
            literals[package] = rows
    return literals


BLUEPRINT_LITERALS = load_literals()


# ---- sizes: stock-checked floats (bp_float) and the package rewrite for values the cook did not store ----
def float_value(row, sizes):
    return float(sizes[row["size"]]) if "size" in row else float(row["value"])


def bp_float(uexp, package, sizes):
    """A blueprint's layout values (floats; doubles for Spacer sizes) set from [Sizes] or fixed; refuses an unexpected stock
    value (a game update moved it): nothing is patched then."""
    out = bytearray(uexp)
    for row in LAYOUT[package].get("floats", []):
        fmt = "<d" if row.get("type") == "double" else "<f"
        width = struct.calcsize(fmt)
        found = struct.unpack_from(fmt, uexp, row["offset"])[0] if len(uexp) >= row["offset"] + width else None
        if found is None or abs(found - row["stock"]) > 1e-3:
            raise SkipAsset(f"{package}: stock size {found} at byte {row['offset']} ({row['widget']}), expected {row['stock']} "
                            f"(game update? re-scan the asset)")
        struct.pack_into(fmt, out, row["offset"], float_value(row, sizes))
    for row in LAYOUT[package].get("bytes", []):   # stored enum bytes (C.1: a HorizontalBoxSlot's Size rule, 0 Auto / 1 Fill)
        found = uexp[row["offset"]] if len(uexp) > row["offset"] else None
        if found != row["stock"]:
            raise SkipAsset(f"{package}: stock byte {found} at {row['offset']} ({row['widget']}), expected {row['stock']} "
                            f"(game update? re-scan the asset)")
        out[row["offset"]] = row["value"]
    return bytes(out)


SKIP_MASK, ZERO_BIT, LAST_BIT, VALUE_SHIFT = 0x7F, 0x80, 0x100, 9


def parse_header(data, pos):
    """An unversioned property header (UE 5.3) -> (fields, zeroed fields, position after it). KB techniques/
    unversioned-package-rewrite.md: uint16 fragments (bits 0-6 SkipNum, 7 bHasAnyZeroes, 8 bIsLast, 9-15 ValueNum), then a
    zero mask over the values of the flagged fragments."""
    frags = []
    while True:
        (packed,) = struct.unpack_from("<H", data, pos)
        pos += 2
        frags.append((packed & SKIP_MASK, bool(packed & ZERO_BIT), packed >> VALUE_SHIFT))
        if packed & LAST_BIT:
            break
    fields, flags, index = [], [], 0
    for skip, zeroes, values in frags:
        index += skip
        for _ in range(values):
            fields.append(index)
            flags.append(zeroes)
            index += 1
    masked = [f for f, z in zip(fields, flags) if z]
    zeroed = set()
    if masked:
        n = len(masked)
        size = 1 if n <= 8 else 2 if n <= 16 else 4 * ((n + 31) // 32)
        bits = int.from_bytes(data[pos:pos + size], "little")
        pos += size
        zeroed = {f for i, f in enumerate(masked) if bits >> i & 1}
    return fields, zeroed, pos


def build_header(fields, zeroes=()):
    """The inverse of parse_header (written like the engine: a fragment is flagged only when one of its own values is zero)."""
    fields, zeroes = sorted(fields), set(zeroes)
    if not fields:
        return struct.pack("<H", LAST_BIT)
    frags, prev = [], -1
    for f in fields:
        if frags and f == prev + 1 and len(frags[-1][1]) < 127:
            frags[-1][1].append(f)
        else:
            skip = f - prev - 1
            while skip > 127:
                frags.append([127, []])
                skip -= 127
            frags.append([skip, [f]])
        prev = f
    out, masked = b"", []
    for i, (skip, values) in enumerate(frags):
        flagged = any(v in zeroes for v in values)
        masked += values if flagged else []
        out += struct.pack("<H", skip | (ZERO_BIT if flagged else 0) | (len(values) << VALUE_SHIFT)
                           | (LAST_BIT if i == len(frags) - 1 else 0))
    if masked:
        bits = sum(1 << i for i, f in enumerate(masked) if f in zeroes)
        n = len(masked)
        out += bits.to_bytes(1 if n <= 8 else 2 if n <= 16 else 4 * ((n + 31) // 32), "little")
    return out


def add_field(export, header_pos, field_sizes, field, value):
    """The export with `value` (bytes) stored for struct field `field` (skipped or zero-masked before)."""
    fields, zeroes, values_pos = parse_header(export, header_pos)
    if field in fields and field not in zeroes:
        raise SkipAsset(f"field {field} is already stored")
    header = build_header(sorted(set(fields) | {field}), set(zeroes) - {field})
    at = values_pos + sum(field_sizes[f] for f in fields if f < field and f not in zeroes)
    return export[:header_pos] + header + export[values_pos:at] + value + export[at:]


def replace_export(uasset, uexp, serial_offset, serial_size, new_export):
    """One export's bytes swapped: its SerialSize, every later SerialOffset and BulkDataStartOffset follow (offsets count
    from the .uasset start). Refuses anything ambiguous."""
    base = len(uasset)
    start = serial_offset - base
    if start < 0 or start + serial_size > len(uexp):
        raise SkipAsset("export outside the .uexp")
    delta = len(new_export) - serial_size
    new_uexp = uexp[:start] + new_export + uexp[start + serial_size:]
    ua = bytearray(uasset)
    end = base + len(uexp) - 4
    hits = [q for q in range(0, min(1024, base - 7)) if struct.unpack_from("<q", ua, q)[0] == end]
    if len(hits) != 1:
        raise SkipAsset(f"BulkDataStartOffset not found once ({hits})")
    struct.pack_into("<q", ua, hits[0], end + delta)
    rows = {}
    for q in range(0, base - 15):
        size, off = struct.unpack_from("<qq", ua, q)
        if base <= off and 0 < size and off + size <= end:
            rows.setdefault(off, []).append((q, size))
    chain, off = [], base
    while off < end:
        nxt = [(q, size) for q, size in rows.get(off, []) if off + size == end or off + size in rows]
        if len(nxt) != 1:
            raise SkipAsset(f"export table chain broken at {off}")
        chain.append((off, *nxt[0]))
        off += nxt[0][1]
    target = [c for c in chain if c[0] == serial_offset and c[2] == serial_size]
    if len(target) != 1:
        raise SkipAsset("export row not found in the export table")
    struct.pack_into("<q", ua, target[0][1], serial_size + delta)
    for off, q, size in chain:
        if off > serial_offset:
            struct.pack_into("<q", ua, q + 8, off + delta)
    return bytes(ua), new_uexp


def _fstring(data, pos):
    n = struct.unpack_from("<i", data, pos)[0]
    if n < 0:
        return data[pos + 4:pos + 4 - 2 * n].decode("utf-16-le")[:-1], pos + 4 - 2 * n
    return data[pos + 4:pos + 4 + n][:-1].decode("latin-1"), pos + 4 + n


def package_summary(uasset):
    """A UE 5.3 legacy cooked package's summary (retoc to-legacy, unversioned): every offset field with its byte position
    ("@<field>"), the name map [(name, hash1, hash2, start)] and where it ends. KB dark-mode-ui.md "Popup paper" (layout)."""
    s, pos = {}, 0

    def take(field, fmt):
        nonlocal pos
        s["@" + field] = pos
        s[field] = struct.unpack_from(fmt, uasset, pos)[0]
        pos += struct.calcsize(fmt)

    if struct.unpack_from("<I", uasset, 0)[0] != 0x9E2A83C1 or struct.unpack_from("<i", uasset, 4)[0] != -8:
        raise SkipAsset("not a UE5 legacy package summary")
    pos = 28
    if struct.unpack_from("<i", uasset, 24)[0] != 0:
        raise SkipAsset("versioned package (custom versions present)")
    take("TotalHeaderSize", "<i")
    s["PackageName"], pos = _fstring(uasset, pos)
    take("PackageFlags", "<I")
    take("NameCount", "<i"); take("NameOffset", "<i")
    for k in ("SoftObjectPaths", "GatherableTextData", "Export", "Import"):
        take(k + "Count", "<i"); take(k + "Offset", "<i")
    take("DependsOffset", "<i")
    take("SoftPackageReferencesCount", "<i"); take("SoftPackageReferencesOffset", "<i")
    take("SearchableNamesOffset", "<i"); take("ThumbnailTableOffset", "<i")
    pos += 16   # Guid
    pos += 4 + 8 * struct.unpack_from("<i", uasset, pos)[0]   # generations
    for _ in range(2):   # saved-by and compatible-with engine versions
        pos += 10
        pos = _fstring(uasset, pos)[1]
    take("CompressionFlags", "<I"); take("CompressedChunks", "<i"); take("PackageSource", "<I")
    take("AdditionalPackagesToCook", "<i")
    if s["CompressedChunks"] or s["AdditionalPackagesToCook"]:
        raise SkipAsset("unexpected compressed chunks or packages to cook in the summary")
    take("AssetRegistryDataOffset", "<i"); take("BulkDataStartOffset", "<q"); take("WorldTileInfoDataOffset", "<i")
    pos += 4 + 4 * struct.unpack_from("<i", uasset, pos)[0]   # chunk ids
    take("PreloadDependencyCount", "<i"); take("PreloadDependencyOffset", "<i")
    take("NamesReferencedFromExportDataCount", "<i"); take("PayloadTocOffset", "<q"); take("DataResourceOffset", "<i")
    s["end"] = pos
    if s["NameOffset"] != pos:
        raise SkipAsset(f"the name map does not follow the summary ({s['NameOffset']} != {pos})")
    names, pos = [], s["NameOffset"]
    for _ in range(s["NameCount"]):
        start = pos
        name, pos = _fstring(uasset, pos)
        h1, h2 = struct.unpack_from("<HH", uasset, pos)
        pos += 4
        names.append((name, h1, h2, start))
    s["names"], s["names_end"] = names, pos
    if s["ImportOffset"] != pos or s["ExportOffset"] != s["ImportOffset"] + 32 * s["ImportCount"] \
            or s["DependsOffset"] != s["ExportOffset"] + 96 * s["ExportCount"]:
        raise SkipAsset("unexpected package layout (name map, 32-byte imports, 96-byte exports)")
    return s


OFFSET_FIELDS = (("TotalHeaderSize", "<i"), ("SoftObjectPathsOffset", "<i"), ("GatherableTextDataOffset", "<i"),
                 ("ExportOffset", "<i"), ("ImportOffset", "<i"), ("DependsOffset", "<i"), ("SoftPackageReferencesOffset", "<i"),
                 ("SearchableNamesOffset", "<i"), ("ThumbnailTableOffset", "<i"), ("AssetRegistryDataOffset", "<i"),
                 ("BulkDataStartOffset", "<q"), ("WorldTileInfoDataOffset", "<i"), ("PreloadDependencyOffset", "<i"),
                 ("PayloadTocOffset", "<q"), ("DataResourceOffset", "<i"))


def swap_imports(uasset, swaps):
    """The package with each import swapped (RMI-096, Dark Mode UI's route): the import row of from_object (class from_class)
    becomes a to_class import of to_object; its outer package row's name and the object's name are renamed in the name map (both
    must be used by those rows only), and every offset after the name map (summary fields, export serial offsets) moves by the
    size change. Refuses anything unexpected (SkipAsset); the .uexp is unchanged."""
    s = package_summary(uasset)
    names = [n[0] for n in s["names"]]
    ua = bytearray(uasset)
    imp, exp = s["ImportOffset"], s["ExportOffset"]
    rows = [list(struct.unpack_from("<qqiqi", ua, imp + 32 * i)) for i in range(s["ImportCount"])]
    exports = [struct.unpack_from("<q", ua, exp + 96 * i + 16)[0] for i in range(s["ExportCount"])]
    for sw in swaps:
        try:
            pkg_n, obj_n, cls_n = (names.index(sw[k]) for k in ("from_package", "from_object", "from_class"))
            to_cls = names.index(sw["to_class"])
        except ValueError as error:
            raise SkipAsset(f"import {sw['from_object']} not found ({error})")
        if sw["to_package"] in names or sw["to_object"] in names:
            raise SkipAsset(f"{sw['to_object']} is already named in the package")
        hits = [i for i, r in enumerate(rows) if r[3] == obj_n and r[1] == cls_n]
        if len(hits) != 1 or rows[hits[0]][2] >= 0 or rows[-rows[hits[0]][2] - 1][3] != pkg_n:
            raise SkipAsset(f"import {sw['from_object']} ({sw['from_class']}) not found once with its package")
        row, outer = hits[0], -rows[hits[0]][2] - 1
        users = [i for i, r in enumerate(rows) for f in (0, 1, 3) if r[f] in (pkg_n, obj_n)] + \
                [i for i, n in enumerate(exports) if n in (pkg_n, obj_n)]
        if sorted(users) != sorted([row, outer]):
            raise SkipAsset(f"{sw['from_object']}'s names are used by other rows ({users})")
        if names[rows[row][0]] != "/Script/Engine":
            raise SkipAsset(f"{sw['from_object']} is not an Engine class import")
        rows[row][1] = to_cls
        names[pkg_n], names[obj_n] = sw["to_package"], sw["to_object"]
    for i, r in enumerate(rows):
        struct.pack_into("<qqiqi", ua, imp + 32 * i, *r)
    name_map = b"".join(struct.pack("<i", len(n) + 1) + n.encode("latin-1") + b"\0" + struct.pack("<HH", h1, h2)
                        for n, (_, h1, h2, _) in zip(names, s["names"]))
    old_end = s["names_end"]
    delta = len(name_map) - (old_end - s["NameOffset"])
    for field, fmt in OFFSET_FIELDS:
        if s[field] >= old_end:
            struct.pack_into(fmt, ua, s["@" + field], s[field] + delta)
    for i in range(s["ExportCount"]):
        at = exp + 96 * i + 36
        struct.pack_into("<q", ua, at, struct.unpack_from("<q", ua, at)[0] + delta)
    out = bytes(ua[:s["NameOffset"]]) + name_map + bytes(ua[old_end:])
    if package_summary(out)["TotalHeaderSize"] != len(out):
        raise SkipAsset("swapped header size mismatch")
    return out


def masked_sha256(data, mask):
    """SHA-256 of `data` with each [start, length] range of `mask` zeroed: a rewrite's stock check that ignores bytes another
    recipe bakes first (RMI-092: the Custom colour of inv_cont_title sits in the export that gains its width)."""
    data = bytearray(data)
    for start, length in mask:
        data[start:start + length] = bytes(length)
    return hashlib.sha256(bytes(data)).hexdigest()


def rewrite_package(uasset, uexp, rewrites):
    """The package with every rewrite applied (a stored value added to a struct of one export; KB techniques/
    unversioned-package-rewrite.md). Each export is checked against its stock bytes (SHA-256) and header first."""
    for r in sorted(rewrites, key=lambda r: -r["export_start"]):   # later exports first: earlier starts stay valid
        start, size = r["export_start"], r["export_size"]
        export = uexp[start:start + size]
        if masked_sha256(export, r.get("sha_mask", [])) != r["stock_export_sha256"]:
            raise SkipAsset(f"{r['widget']}: stock export differs from the verified game build (game update?)")
        fields, zeroes, _ = parse_header(export, r["header_pos"])
        if fields != r["stock_fields"] or sorted(zeroes) != r["stock_zeroes"]:
            raise SkipAsset(f"{r['widget']}: unexpected property header {fields} {sorted(zeroes)}")
        sizes = dict(enumerate(r["field_sizes"]))
        for field, value in r.get("add", []):
            export = add_field(export, r["header_pos"], sizes, field, struct.pack("<f", value))
        for field, value in r.get("add_bytes", []):   # a whole value as cooked bytes (C.1: an FSlateChildSize struct)
            export = add_field(export, r["header_pos"], sizes, field, bytes.fromhex(value))
        uasset, uexp = replace_export(uasset, uexp, len(uasset) + start, size, export)
    return uasset, uexp


def layout_package(uasset, uexp, package, sizes):
    """A package's layout: the stored values in place, then the rewrites (which move later bytes)."""
    uexp = bp_float(uexp, package, sizes)
    if LAYOUT[package].get("rewrites"):
        uasset, uexp = rewrite_package(uasset, uexp, LAYOUT[package]["rewrites"])
    return uasset, uexp


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


def bc1_header_size(data):
    """The DDS header size of a BC1 texture (8-byte blocks), or None for any other format."""
    if data[:4] != b"DDS ":
        return None
    if data[84:88] == b"DXT1":
        return 128
    if data[84:88] == b"DX10" and int.from_bytes(data[128:132], "little") in (71, 72):
        return 148
    return None


def rgb565(rgb):
    r, g, b = (round(c) for c in rgb)
    return (round(r * 31 / 255) << 11) | (round(g * 63 / 255) << 5) | round(b * 31 / 255)


def flat_blocks(data, rgb):
    """Every BC3 colour block = one RGB565 colour (index 0); the alpha blocks are kept byte for byte. An uncompressed 32-bit
    texture (the B8G8R8A8 altar separators, RMI-087) gets the colour in every pixel, alpha kept."""
    if bgra_layout(data):
        return map_colours(data, lambda c: rgb)
    out = bytearray(data)
    colour = rgb565(rgb).to_bytes(2, "little")
    for offset in range(header_size(data), len(out), 16):
        out[offset + 8:offset + 16] = colour + colour + bytes(4)
    return bytes(out)


def bc3_alpha_block(values):
    """16 alpha values (0-255, row order) as one BC3 alpha block in the 8-value mode (alpha0 > alpha1; all equal: one value)."""
    hi, lo = max(values), min(values)
    if hi == lo:
        return bytes((hi, lo)) + bytes(6)
    palette = [hi, lo] + [((7 - k) * hi + k * lo) / 7 for k in range(1, 7)]
    bits = 0
    for i, v in enumerate(values):
        bits |= min(range(8), key=lambda c: abs(palette[c] - v)) << (3 * i)
    return bytes((hi, lo)) + bits.to_bytes(6, "little")


def flat_alpha(data, rgb, scale, dds):
    """flat_blocks (one colour) with the alpha times scale: the BC3 alpha blocks re-encoded from the decoded stock alpha
    (the run-time RowEquipped.png: gold, alpha x 0.6, decision 28)."""
    out = bytearray(flat_blocks(data, rgb))
    alpha = Image.open(dds).convert("RGBA").getchannel("A")
    width, height = alpha.size
    at = header_size(data)
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            values = [round(alpha.getpixel((min(bx + x, width - 1), min(by + y, height - 1))) * scale) for y in range(4) for x in range(4)]
            out[at:at + 8] = bc3_alpha_block(values)
            at += 16
    return bytes(out)


def clear_alpha(data):
    """Every BC3 alpha block fully transparent, the colour blocks kept (the run-time hidden equipped scribble, decision 28)."""
    out = bytearray(data)
    for offset in range(header_size(data), len(out), 16):
        out[offset:offset + 8] = bytes(8)
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


def bgra_layout(data):
    """(header size, byte order of R, G, B, A in a pixel) of an uncompressed 32-bit DDS (DXGI 87/88 or a legacy RGB header with
    masks), or None. T_UI_Spellmaking-Frame is B8G8R8A8 with one mip."""
    if data[:4] != b"DDS ":
        return None
    if data[84:88] == b"DX10" and int.from_bytes(data[128:132], "little") in (87, 88):
        return 148, (2, 1, 0, 3)
    flags, bits = int.from_bytes(data[80:84], "little"), int.from_bytes(data[88:92], "little")
    if flags & 0x40 and bits == 32:
        masks = [int.from_bytes(data[92 + 4 * i:96 + 4 * i], "little") for i in range(4)]
        order = tuple((m.bit_length() - 1) // 8 if m else 3 for m in masks)
        return 128, order
    return None


def r8_header_size(data):
    """The DDS header size of a single-channel 8-bit texture (PF_G8 = DXGI 61 R8_UNORM: the popup paper T_Paper_Wrinkled_D,
    RMI-096), or None for any other format."""
    if data[:4] == b"DDS " and data[84:88] == b"DX10" and int.from_bytes(data[128:132], "little") == 61:
        return 148
    return None


def texture_header(data):
    """The DDS header size of a texture the colour recipes can map: BC3, BC1, uncompressed 32-bit or R8 (paper_scale only);
    AssertionError otherwise."""
    for size in (bc1_header_size(data), (bgra_layout(data) or (None,))[0], r8_header_size(data)):
        if size:
            return size
    return header_size(data)


def map_colours(data, fn):
    """Every colour of a texture through fn((r, g, b) sRGB) -> (r, g, b): BC3 and BC1 colour endpoints (indices and BC3 alpha
    blocks kept; a BC1 block keeps its four-colour or transparent mode), or every pixel of an uncompressed texture (alpha kept)."""
    out = bytearray(data)
    layout = bgra_layout(data)
    if layout:
        head, (r, g, b, _) = layout
        for at in range(head, len(out) - 3, 4):
            c = fn((out[at + r], out[at + g], out[at + b]))
            out[at + r], out[at + g], out[at + b] = (max(0, min(255, round(v))) for v in c)
        return bytes(out)

    def mapped(e):
        rgb = (((e >> 11) & 31) * 255 / 31, ((e >> 5) & 63) * 255 / 63, (e & 31) * 255 / 31)
        return rgb565(tuple(max(0, min(255, round(v))) for v in fn(rgb)))

    bc1 = bc1_header_size(data)
    if bc1 is None:
        for offset in range(header_size(data) + 8, len(out), 16):
            for at in (offset, offset + 2):
                out[at:at + 2] = mapped(int.from_bytes(out[at:at + 2], "little")).to_bytes(2, "little")
        return bytes(out)
    for at in range(bc1, len(out) - 7, 8):
        c0, c1 = int.from_bytes(out[at:at + 2], "little"), int.from_bytes(out[at + 2:at + 4], "little")
        n0, n1 = mapped(c0), mapped(c1)
        bits = int.from_bytes(out[at + 4:at + 8], "little")
        if c0 > c1 and n0 <= n1:
            if n0 == n1:
                bits = 0
            else:
                n0, n1 = n1, n0
                bits ^= 0x55555555
        elif c0 <= c1 and n0 > n1:
            n0, n1 = n1, n0
            bits = sum(((v ^ 1) if v < 2 else v) << (2 * i) for i, v in ((i, (bits >> (2 * i)) & 3) for i in range(16)))
        out[at:at + 2], out[at + 2:at + 4] = n0.to_bytes(2, "little"), n1.to_bytes(2, "little")
        out[at + 4:at + 8] = bits.to_bytes(4, "little")
    return bytes(out)


def clamp01(v):
    return min(1.0, max(0.0, v))


ALTAR_REF, ALTAR_CONTRAST = 150, 0.8   # stock brightness drawn at the key colour; the relief exponent (fitted to the owner art)


def altar_metal(rgb, metal):
    """One colour times the stock brightness (the owner art's crystal, ornaments and frame metal, KB dark-mode-ui.md)."""
    k = (lum(rgb) / ALTAR_REF) ** ALTAR_CONTRAST if lum(rgb) > 0 else 0.0
    return tuple(c * k for c in metal)


def altar_panel(rgb, metal, paper):
    """A right-panel frame: stock paper (bright, little colour) -> the paper colour; the metal -> altar_metal."""
    w = clamp01((lum(rgb) - 140) / 25) * clamp01((45 - (max(rgb) - min(rgb))) / 20)
    m = altar_metal(rgb, metal)
    return tuple(p * w + c * (1 - w) for p, c in zip(paper, m))


def decode_rgba(stock_dds):
    """The top mip of a stock texture as an RGBA image (BC3/BC1 through Pillow, uncompressed 32-bit read raw)."""
    data = stock_dds.read_bytes()
    layout = bgra_layout(data)
    if not layout:
        return Image.open(stock_dds).convert("RGBA")
    head, order = layout
    height, width = int.from_bytes(data[12:16], "little"), int.from_bytes(data[16:20], "little")
    raw = data[head:head + width * height * 4]
    channels = [Image.frombytes("L", (width, height), bytes(raw[i::4])) for i in range(4)]
    return Image.merge("RGBA", [channels[order[i]] for i in range(4)])


# a panel's own paper mask (texture, channel): exact paper area where the stock paper has dark stains (KB dark-mode-ui.md)
PANEL_MASKS = {"T_Enchantment_EnchantingBackground": ("T_Enchantment_Masks", 0)}


def altar_panel_texture(stock_dds, metal, paper, work, mask=None):
    """A right-panel frame recoloured per pixel (owner run 2026-11-11: block-wise endpoints left metal-coloured specks in the
    paper, and the gems were recoloured): the paper weight map is median-filtered so grain specks stay paper, the paper keeps
    a soft grain, and stock blue/cyan gems keep their colour. BC3 re-encoded with the stock alpha; uncompressed written as is."""
    from PIL import ImageFilter
    image = decode_rgba(stock_dds)
    px = image.get_flattened_data()
    lums = [lum(p) for p in px]
    weight = Image.new("L", image.size)
    weight.putdata([round(255 * clamp01((v - 140) / 25) * clamp01((45 - (max(p[:3]) - min(p[:3]))) / 20)) for p, v in zip(px, lums)])
    weight = weight.filter(ImageFilter.MedianFilter(9)).filter(ImageFilter.GaussianBlur(1.2))
    if mask is not None:   # the panel's paper mask replaces the brightness rule
        path, channel = mask
        weight = Image.open(path).convert("RGBA").getchannel(channel).resize(image.size, Image.BILINEAR).filter(ImageFilter.GaussianBlur(1.0))
    gem = Image.new("L", image.size)
    gem.putdata([round(255 * clamp01((p[2] - p[0] - 20) / 25) * clamp01((max(p[:3]) - min(p[:3]) - 30) / 30)) for p in px])
    gem = gem.filter(ImageFilter.MedianFilter(3))
    paper_l = [v for v, w in zip(lums, weight.get_flattened_data()) if w > 200]
    mean = sum(paper_l) / len(paper_l) if paper_l else 200.0
    out = []
    for p, v, w, g in zip(px, lums, weight.get_flattened_data(), gem.get_flattened_data()):
        w, g = w / 255, g / 255
        grain = min(1.2, max(0.8, (v / mean) ** 0.6)) if v > 0 else 0.8
        m = altar_metal(p, metal)
        c = [pc * grain * w + mc * (1 - w) for pc, mc in zip(paper, m)]
        c = [cc * (1 - g) + sc * g for cc, sc in zip(c, p[:3])]
        out.append(tuple(max(0, min(255, round(x))) for x in c) + (p[3],))
    result = Image.new("RGBA", image.size)
    result.putdata(out)
    data = stock_dds.read_bytes()
    layout = bgra_layout(data)
    if not layout:
        return colour_keep_alpha(stock_dds, result, work)
    head, order = layout
    raw = bytearray(data)
    flat = result.tobytes()
    for i in range(image.size[0] * image.size[1]):
        for c in range(3):   # alpha stays the stock byte
            raw[head + 4 * i + order[c]] = flat[4 * i + c]
    return bytes(raw)


def colour_bc1(stock_dds, art, work):
    """An RGBA image into a stock BC1 texture: every mip level BC1-encoded by Pillow (its 1-bit alpha from the image's alpha),
    whole 8-byte blocks replaced; the stock header (size, mips, format) stays."""
    stock = stock_dds.read_bytes()
    head = bc1_header_size(stock)
    height, width = int.from_bytes(stock[12:16], "little"), int.from_bytes(stock[16:20], "little")
    blocks = bytearray(stock[head:])
    at = 0
    for size in mip_sizes(width, height, int.from_bytes(stock[28:32], "little")):
        level = work / "level1.dds"
        art.resize(size, Image.LANCZOS).save(level, pixel_format="DXT1")
        data = level.read_bytes()
        encoded = data[bc1_header_size(data):]
        blocks[at:at + len(encoded)] = encoded
        at += len(encoded)
    assert at == len(blocks), "size mismatch"
    return stock[:head] + bytes(blocks)


# Shield, crystal and skull (and their arrows) have the same positions in all
# three stock strips. Read each group from the variant with the lightest paper
# there. Limit contrast detection to the artwork so torn paper edges stay dark.
TAB_STRIP_ART = {
    "T_Enchantment_Tab_Gear": ((90, 34, 125, 69), (168, 9, 239, 94), (281, 33, 317, 69)),
    "T_Enchantment_Tab_SoulGem": ((399, 33, 434, 69), (483, 13, 545, 90), (591, 34, 625, 69)),
    "T_Enchantment_Tab_Effect": ((708, 34, 742, 69), (780, 9, 862, 93), (899, 34, 934, 69)),
}


def tab_strip_mask(image, stock):
    """Extract one shared mask from the three stock variants."""
    from PIL import ImageFilter

    mask = Image.new("L", image.size)
    if image.size != (1024, 128):
        raise SkipAsset("the stock tab strip has unexpected dimensions")
    for name, zones in TAB_STRIP_ART.items():
        source = decode_rgba(stock(name))
        if source.size != image.size:
            raise SkipAsset("the stock tab strips have different dimensions")
        gray = source.convert("L")
        local = gray.filter(ImageFilter.MedianFilter(41))
        for zone in zones:
            pixels = source.crop(zone).get_flattened_data()
            paper = local.crop(zone).get_flattened_data()
            strokes = Image.new("L", (zone[2] - zone[0], zone[3] - zone[1]))
            strokes.putdata([round(255 * clamp01((m - lum(p) - 11) / 24)) for p, m in zip(pixels, paper)])
            mask.paste(strokes, zone)
    return mask


def tab_strip_texture(stock_dds, icon, paper, work, stock=None):
    """Recolour the shared icons and arrows over the variant's darkened paper."""
    from PIL import ImageFilter

    image = decode_rgba(stock_dds)
    pixels = image.get_flattened_data()
    if stock is not None:
        weights = [w / 255 for w in tab_strip_mask(image, stock).get_flattened_data()]
    else:
        # A standalone texture can still be recoloured without a stock lookup.
        gray = image.convert("L")
        local = gray.filter(ImageFilter.MedianFilter(15)).get_flattened_data()
        weights = [clamp01((m - v - 12) / 25) for v, m in zip(gray.get_flattened_data(), local)]

    out = []
    for p, w in zip(pixels, weights):
        shade = (8 + 0.10 * lum(p)) / 27
        rgb = tuple(min(255, round(i * w + c * shade * (1 - w))) for i, c in zip(icon, paper))
        out.append(rgb + (p[3],))
    result = Image.new("RGBA", image.size)
    result.putdata(out)
    encode = colour_bc1 if bc1_header_size(stock_dds.read_bytes()) else colour_keep_alpha
    return encode(stock_dds, result, work)


def parchment_curve(v, low, high, dark, light):
    """Stock brightness low..high -> dark..light (clamped): the parchment's grain kept in a narrow neutral gray band."""
    t = min(1.0, max(0.0, (v - low) / (high - low))) if high > low else 0.5
    return dark + (light - dark) * t


def _rgb_of(e):
    return (((e >> 11) & 31) * 255 / 31, ((e >> 5) & 63) * 255 / 63, (e & 31) * 255 / 31)


_GREY_LEVELS = sorted({gray565(v) for v in range(256)})   # neutral 565 codes, increasing code = increasing grey


def grey_blocks(data, grey):
    """Every BC3/BC1 colour block re-encoded in neutral greys: its 16 stock colours are decoded, each mapped by grey(brightness)
    to a target grey, and the block gets the two neighbouring grey levels around its targets plus the interpolated 1/3 and 2/3
    steps, each pixel taking the nearest. Smooth gradients stay smooth (owner run 2026-11-11: endpoint-only mapping snapped
    each block to a coarse level). BC1 blocks in transparent mode keep their mode (endpoints mapped only)."""
    out = bytearray(data)
    bc1 = bc1_header_size(data)
    head, stride, first = (bc1, 8, 0) if bc1 else (header_size(data), 16, 8)
    lums = {}

    def lum_of(e):
        if e not in lums:
            lums[e] = lum(_rgb_of(e))
        return lums[e]

    for at in range(head + first, len(out) - 7, stride):
        c0, c1 = int.from_bytes(out[at:at + 2], "little"), int.from_bytes(out[at + 2:at + 4], "little")
        bits = int.from_bytes(out[at + 4:at + 8], "little")
        if bc1 and c0 <= c1:   # three colours and transparent: endpoints only, mode kept
            n0, n1 = gray565(grey(lum_of(c0))), gray565(grey(lum_of(c1)))
            if n0 > n1:
                n0, n1 = n1, n0
                bits = sum(((v ^ 1) if v < 2 else v) << (2 * i) for i, v in ((i, (bits >> (2 * i)) & 3) for i in range(16)))
            out[at:at + 2], out[at + 2:at + 4], out[at + 4:at + 8] = n0.to_bytes(2, "little"), n1.to_bytes(2, "little"), bits.to_bytes(4, "little")
            continue
        l0, l1 = lum_of(c0), lum_of(c1)
        palette = (l0, l1, (2 * l0 + l1) / 3, (l0 + 2 * l1) / 3)
        targets = [grey(palette[(bits >> (2 * i)) & 3]) for i in range(16)]
        lo_t, hi_t = min(targets), max(targets)
        lo = max((c for c in _GREY_LEVELS if lum_of(c) <= lo_t + 1e-6), default=_GREY_LEVELS[0])
        hi = min((c for c in _GREY_LEVELS if lum_of(c) >= hi_t - 1e-6), default=_GREY_LEVELS[-1])
        if hi <= lo:
            i = _GREY_LEVELS.index(lo)
            if i + 1 < len(_GREY_LEVELS):
                hi = _GREY_LEVELS[i + 1]
            else:
                lo, hi = _GREY_LEVELS[i - 1], lo
        a, b = lum_of(hi), lum_of(lo)
        steps = (a, b, (2 * a + b) / 3, (a + 2 * b) / 3)
        new_bits = sum(min(range(4), key=lambda k: abs(steps[k] - v)) << (2 * i) for i, v in enumerate(targets))
        out[at:at + 2], out[at + 2:at + 4], out[at + 4:at + 8] = hi.to_bytes(2, "little"), lo.to_bytes(2, "little"), new_bits.to_bytes(4, "little")
    return bytes(out)


def parchment(stock_dds, dark, light):
    """Every pixel's brightness mapped to a neutral grey by the parchment curve over the texture's own brightness range
    (opaque pixels, 2nd..98th percentile), encoded by grey_blocks (BC3 alpha blocks untouched; BC1 modes kept)."""
    image = decode_rgba(stock_dds)
    values = sorted(round(lum(p)) for p in image.get_flattened_data() if p[3] > 128) or [0, 255]
    low, high = values[len(values) * 2 // 100], values[len(values) * 98 // 100]
    return grey_blocks(stock_dds.read_bytes(), lambda v: parchment_curve(v, low, high, dark, light))


def paper_scale(stock_dds, light):
    """The popup paper darkened evenly (RMI-096, owner 2026-11-13 "Keep stock grain"): every brightness times light / the
    texture's 98th-percentile brightness, so the light end lands on `light` and the grain keeps its stock ratio (parchment's
    dark..light stretch made the faint creases harsh). R8 (PF_G8: T_Paper_Wrinkled_D) bytes directly; BC1/BC3 through
    grey_blocks (T_Popup_Flowers_D: its black background stays black)."""
    data = stock_dds.read_bytes()
    head = r8_header_size(data)
    values = sorted(data[head:]) if head else sorted(round(lum(p)) for p in decode_rgba(stock_dds).get_flattened_data() if p[3] > 128)
    high = (values or [255])[len(values) * 98 // 100] or 255
    scale = lambda v: max(0, min(255, round(v * light / high)))   # noqa: E731
    if head:
        return data[:head] + bytes(scale(v) for v in data[head:])
    return grey_blocks(data, scale)


GREY5 = [(k << 3) | (k >> 2) for k in range(32)]   # a 5-bit BC colour level as the decoder expands it
BAYER4 = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)


def neutral_grey_colour(blocks, grey, dither=False):
    """BC3 blocks (alpha halves kept) with every colour half rewritten as exact neutral grey (565 green = 2 x red/blue, so no
    green cast): the endpoints bracket the block's range on the 5-bit grey ladder and each pixel takes the nearest of the four
    palette greys (thirds between the endpoints); `dither` offsets each pixel by a 4x4 ordered (Bayer) pattern of up to one
    third, a fixed pattern, not random grain. Pillow's own BC3 encoder bands dark gradients into 8-level steps and tints them
    green (RMI-096, KB lessons/tooling.md)."""
    out = bytearray(blocks)
    width, height = grey.size
    values = grey.get_flattened_data() if hasattr(grey, "get_flattened_data") else grey.getdata()
    values = list(values)
    at = 0
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            block = [values[min(by + y, height - 1) * width + min(bx + x, width - 1)] for y in range(4) for x in range(4)]
            lo, hi = min(block), max(block)
            k0 = max([k for k in range(32) if GREY5[k] <= lo] or [0])
            k1 = min([k for k in range(32) if GREY5[k] >= hi] or [31])
            if k1 <= k0:
                k0, k1 = (k0, k0 + 1) if k0 < 31 else (30, 31)
            p0, p1 = GREY5[k1], GREY5[k0]
            palette = (p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3)
            third = (p0 - p1) / 3
            index = 0
            for i, v in enumerate(block):
                if dither:
                    v += (BAYER4[i] / 16 - 0.5) * third
                index |= min(range(4), key=lambda j: abs(palette[j] - v)) << (2 * i)
            struct.pack_into("<HHI", out, at + 8, (k1 << 11) | ((2 * k1) << 5) | k1, (k0 << 11) | ((2 * k0) << 5) | k0, index)
            at += 16
    return bytes(out)


def neutral_grey_fs(blocks, grey):
    """BC3 blocks (alpha halves kept) with every colour half rewritten as exact neutral grey like neutral_grey_colour, but the
    pixels quantised to their block's four palette greys with Floyd-Steinberg error diffusion (7/16, 3/16, 5/16, 1/16, raster
    order across block borders): no repeating 4x4 pattern (RMI-091, owner 2026-11-13: the ordered dither showed as lines and
    points on the stretched container panel). Endpoints bracket each block's target range on the 5-bit grey ladder."""
    out = bytearray(blocks)
    width, height = grey.size
    flat = list(grey.get_flattened_data() if hasattr(grey, "get_flattened_data") else grey.getdata())
    rows = [[float(v) for v in flat[y * width:(y + 1) * width]] for y in range(height)]
    bw, bh = (width + 3) // 4, (height + 3) // 4
    ends = []
    for by in range(bh):
        for bx in range(bw):
            vals = [rows[min(by * 4 + y, height - 1)][min(bx * 4 + x, width - 1)] for y in range(4) for x in range(4)]
            lo, hi = min(vals), max(vals)
            k0 = max([k for k in range(32) if GREY5[k] <= lo] or [0])
            k1 = min([k for k in range(32) if GREY5[k] >= hi] or [31])
            if k1 <= k0:
                k0, k1 = (k0, k0 + 1) if k0 < 31 else (30, 31)
            p0, p1 = GREY5[k1], GREY5[k0]
            ends.append((k0, k1, (p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3)))
    index = [[0] * width for _ in range(height)]
    for y in range(height):
        row, below = rows[y], rows[y + 1] if y + 1 < height else None
        base = (y // 4) * bw
        for x in range(width):
            palette = ends[base + x // 4][2]
            v = row[x]
            j = min(range(4), key=lambda k: abs(palette[k] - v))
            index[y][x] = j
            err = v - palette[j]
            if x + 1 < width:
                row[x + 1] += err * 7 / 16
            if below is not None:
                if x > 0:
                    below[x - 1] += err * 3 / 16
                below[x] += err * 5 / 16
                if x + 1 < width:
                    below[x + 1] += err / 16
    at = 0
    for by in range(bh):
        for bx in range(bw):
            k0, k1, _ = ends[by * bw + bx]
            bits = 0
            for i in range(16):
                bits |= index[min(by * 4 + i // 4, height - 1)][min(bx * 4 + i % 4, width - 1)] << (2 * i)
            struct.pack_into("<HHI", out, at + 8, (k1 << 11) | ((2 * k1) << 5) | k1, (k0 << 11) | ((2 * k0) << 5) | k0, bits)
            at += 16
    return bytes(out)


def page_grain(stock_dds, frame, panel, blur, amount, tone=None):
    """The Inventory page (T_UI_PlayerMenu_Background) with a paper grain instead of a flat colour (RMI-091, owner 2026-11-13):
    the `panel` recipe's tone curve (stock brightness PANEL_DARK..PANEL_LIGHT -> frame..panel) plus, on the paper (stock
    brightness above PANEL_LIGHT, faded in over 20), the blurred paper brightness (2nd..98th percentile) as +-amount around
    it; Floyd-Steinberg neutral greys, alpha halves kept."""
    from PIL import ImageFilter
    data = stock_dds.read_bytes()
    head = header_size(data)
    image = decode_rgba(stock_dds)
    grey = grey_of(image)
    values = list(grey.get_flattened_data())
    soft = list(grey.filter(ImageFilter.GaussianBlur(blur)).get_flattened_data())
    paper = sorted(v for v, p in zip(values, image.get_flattened_data()) if v > PANEL_LIGHT + 20 and p[3] > 128) or [0, 255]
    low, high = paper[len(paper) * 2 // 100], paper[len(paper) * 98 // 100]
    span = (high - low) or 1
    panel = panel if tone is None else tone   # the paper's tone: the shared paper tone (RMI-091 round 2)
    target = []
    for v, s in zip(values, soft):
        t = min(1.0, max(0.0, (v - PANEL_DARK) / (PANEL_LIGHT - PANEL_DARK)))
        w = min(1.0, max(0.0, (v - PANEL_LIGHT) / 20))
        grain = (min(1.0, max(0.0, (s - low) / span)) - 0.5) * 2 * amount
        target.append(max(0.0, min(255.0, frame + (panel - frame) * t + w * grain)))
    image = Image.new("F", grey.size)
    image.putdata(target)
    return data[:head] + neutral_grey_fs(data[head:], image)


def band_lines(stock_dds, rgb, scale):
    """The container name band (T_Trading_Title_Background): its two thin lines in rgb x scale (RMI-091, owner "lighter
    accent"), the black shadow band kept black: every colour scaled by its brightness over the texture's brightest opaque
    pixels (99.9th percentile; the lines are ~2% of the pixels, so line_tint's 98th percentile would land in the shadow)."""
    values = sorted(lum(p) for p in decode_rgba(stock_dds).get_flattened_data() if p[3] > 128) or [255]
    high = values[-max(1, len(values) // 1000)] or 255
    target = [min(255.0, c * scale) for c in rgb]
    return map_colours(stock_dds.read_bytes(), lambda c: tuple(min(255.0, x * lum(c) / high) for x in target))


def grey_of(image):
    """The image's per-pixel brightness as an L image."""
    grey = Image.new("L", image.size)
    grey.putdata([round(lum(p)) for p in image.get_flattened_data()])
    return grey


def paper_tone(dark, light, panel):
    """The shared paper tone of the container panel, the Inventory page and the Magic page (RMI-091, owner 2026-11-13:
    "approximate the colours between inv and the containers"): halfway between the parchment band's centre and the panel."""
    return ((dark + light) / 2 + panel) / 2


def smooth_grain(stock_dds, dark, light, blur, amount, mid=None):
    """A paper with a smoother grain (RMI-091, owner 2026-11-13 "blur 1.5, grain 60%"): its brightness Gaussian-blurred by
    `blur` pixels, the 2nd..98th percentile (opaque pixels) mapped onto `amount` of the dark..light band around its middle, then
    BC3-encoded as dithered neutral greys (neutral_grey_colour); the stock alpha halves are kept. The parchment bake's 8..33
    band left ~4 grey levels per block: the blocky grain."""
    from PIL import ImageFilter
    data = stock_dds.read_bytes()
    head = header_size(data)
    image = decode_rgba(stock_dds)
    grey = grey_of(image)
    values = sorted(v for v, p in zip(grey.get_flattened_data(), image.get_flattened_data()) if p[3] > 128) or [0, 255]
    low, high = values[len(values) * 2 // 100], values[len(values) * 98 // 100]
    soft = grey.filter(ImageFilter.GaussianBlur(blur)) if blur else grey
    mid = (dark + light) / 2 if mid is None else mid
    half = (light - dark) / 2 * amount
    span = (high - low) or 1
    lut = [max(0.0, min(255.0, mid - half + 2 * half * min(1.0, max(0.0, (v - low) / span)))) for v in range(256)]
    target = Image.new("F", soft.size)
    target.putdata([lut[v] for v in soft.get_flattened_data()])
    return data[:head] + neutral_grey_fs(data[head:], target)   # round 2: error diffusion, not the ordered dither


def details_smooth(stock_dds, tint, blur, amount):
    """The details page (T_SpellPurchase_LeftPageBG) with smooth_grain in the band its detailsPage tint gives today (stock
    2nd..98th percentile brightness times the linear tint): same tone, smoother grain (RMI-091)."""
    image = decode_rgba(stock_dds)
    values = sorted(round(lum(p)) for p in image.get_flattened_data() if p[3] > 128) or [0, 255]
    dark = srgb(linear(values[len(values) * 2 // 100]) * tint)
    light = srgb(linear(values[len(values) * 98 // 100]) * tint)
    return smooth_grain(stock_dds, dark, light, blur, amount)


def gray_smooth(stock_dds, blur):
    """The preview box (T_InventoryPreview_Background_D): its brightness as grey (like "gray"), blurred by `blur` pixels and
    encoded as dithered neutral greys, stock alpha halves kept; Pillow's encoder banded the glow into green rings (RMI-091)."""
    from PIL import ImageFilter
    data = stock_dds.read_bytes()
    head = header_size(data)
    grey = grey_of(decode_rgba(stock_dds))
    if blur:
        grey = grey.filter(ImageFilter.GaussianBlur(blur))
    return data[:head] + neutral_grey_colour(data[head:], grey, dither=True)


def invert_paper(stock_dds, tone, blur, mode="clamp", frame=None, panel=None):
    """The standard parchment look (RMI-091, owner 2026-11-13: luminance inversion + colour mapping, full strength, blur 3,
    "this combo should be the standard for parchments"): the paper's brightness clamped to its own 2nd..98th percentile,
    Gaussian-blurred by `blur` (non-paper pixels count as the paper median: no bright rim at a torn edge), inverted
    (255 - v) and mapped to tone x inverted / inverted median (the paper median lands on `tone`, every value keeps its ratio).
    mode "clamp": the whole texture is paper (container panel). "frame": pixels at or below stock PANEL_LIGHT keep the panel
    curve (frame..panel, the ornament's relief), blended into the paper over PANEL_LIGHT..+20 (Inventory/Magic page).
    "shadow": the darkest 20% of the opaque pixels are the drop shadow, ramped from black to the paper's edge value (details
    page). Neutral grey Floyd-Steinberg BC3 (neutral_grey_fs), alpha halves kept."""
    from PIL import ImageFilter
    data = stock_dds.read_bytes()
    head = header_size(data)
    image = decode_rgba(stock_dds)
    px = list(image.get_flattened_data())
    values = [lum(p) for p in px]
    edge = PANEL_LIGHT + 20 if mode == "frame" else 0
    paper = sorted(v for v, p in zip(values, px) if p[3] > 128 and v > edge) or [128.0]
    if mode == "shadow":
        paper = paper[len(paper) // 5:]
    low, high = paper[len(paper) * 2 // 100], paper[len(paper) * 98 // 100]
    middle = paper[len(paper) // 2]
    inv_middle = (255 - middle) or 1

    def mapped(v):
        return tone * (255 - min(high, max(low, v))) / inv_middle

    soft = values
    if blur:
        source = Image.new("L", image.size)
        source.putdata([round(min(high, max(low, v))) if (p[3] > 128 and v > edge) else round(middle) for v, p in zip(values, px)])
        soft = list(source.filter(ImageFilter.GaussianBlur(blur)).get_flattened_data())
    target = []
    for v, s in zip(values, soft):
        out = mapped(s)
        if mode == "frame":
            t = min(1.0, max(0.0, (v - PANEL_DARK) / (PANEL_LIGHT - PANEL_DARK)))
            f = frame + (panel - frame) * t
            w = min(1.0, max(0.0, (v - PANEL_LIGHT) / 20))
            out = f + (out - f) * w
        elif mode == "shadow" and v < low:
            out = mapped(low) * v / low
        target.append(max(0.0, min(255.0, out)))
    grey = Image.new("F", image.size)
    grey.putdata(target)
    return data[:head] + neutral_grey_fs(data[head:], grey)


def details_tone(stock_dds, tint):
    """The details page's tone: the middle of the band its detailsPage tint gave (stock 2nd..98th percentile brightness of the
    opaque pixels times the linear tint), ~13 for 0.01."""
    values = sorted(round(lum(p)) for p in decode_rgba(stock_dds).get_flattened_data() if p[3] > 128) or [0, 255]
    dark = srgb(linear(values[len(values) * 2 // 100]) * tint)
    light = srgb(linear(values[len(values) * 98 // 100]) * tint)
    return (dark + light) / 2


def line_tint(stock_dds, rgb, scale):
    """A glow line in the accent colour (RMI-091, owner "lighter accent"): rgb x scale at the texture's brightest part (98th
    percentile of the visible pixels), darker parts darker; BC3 colour endpoints mapped, alpha kept."""
    values = sorted(lum(p) for p in decode_rgba(stock_dds).get_flattened_data() if p[3] > 0) or [255]
    high = values[len(values) * 98 // 100] or 255
    target = [min(255.0, c * scale) for c in rgb]
    return map_colours(stock_dds.read_bytes(), lambda c: tuple(min(255.0, x * lum(c) / high) for x in target))


def popup_panel(stock_dds, paper, side, top, work):
    """T_UI_BackgroundHorizontal for the swapped popups (RMI-096; owner 2026-11-13 chose mock-up D, 15% off the sides, 8% off
    top/bottom): plain paper in `paper`, the edges fading smoothly to 16/33 of it (12% of the width, 22% of the height), a border
    line at 64/33 of it (1 screen pixel, inset ~13 at the popup's usual 800x275 size), keeping the stock torn-edge alpha; then
    the whole panel shrunk into the centre (`side` of the width off each side, `top` of the height off top and bottom),
    transparent around it. Every mip BC3-encoded with this alpha (like stolen_mark)."""
    from PIL import ImageChops, ImageDraw
    stock = stock_dds.read_bytes()
    height, width = int.from_bytes(stock[12:16], "little"), int.from_bytes(stock[16:20], "little")
    mips = max(1, int.from_bytes(stock[28:32], "little"))
    alpha = decode_rgba(stock_dds).getchannel("A")
    ramp = lambda n, frac: [round(255 * min(1.0, min(i, n - 1 - i) / (n * frac))) for i in range(n)]   # noqa: E731
    across, down = Image.new("L", (width, 1)), Image.new("L", (1, height))
    across.putdata(ramp(width, 0.12))
    down.putdata(ramp(height, 0.22))
    depth = ImageChops.darker(across.resize((width, height), Image.NEAREST), down.resize((width, height), Image.NEAREST))
    lut = [255 * (1 - (1 - d / 255) ** 2) for d in range(256)]   # smooth: dark at the edge, full paper inward
    bands = []
    for c in paper[:3]:
        edge = c * 16 / 33
        band = depth.point([max(0, min(255, round(edge + (c - edge) * lut[d] / 255))) for d in range(256)])
        line = max(0, min(255, round(c * 64 / 33)))
        draw = ImageDraw.Draw(band)
        ix, iy = round(width * 34 / 2048), round(height * 52 / 1024)
        lw, lh = max(1, round(width * 3 / 2048)), max(1, round(height * 4 / 1024))
        draw.rectangle((ix, iy, width - 1 - ix, iy + lh - 1), fill=line)
        draw.rectangle((ix, height - iy - lh, width - 1 - ix, height - 1 - iy), fill=line)
        draw.rectangle((ix, iy, ix + lw - 1, height - 1 - iy), fill=line)
        draw.rectangle((width - ix - lw, iy, width - 1 - ix, height - 1 - iy), fill=line)
        bands.append(band)
    panel = Image.merge("RGBA", (*bands, alpha))
    w, h = round(width * (1 - 2 * side)), round(height * (1 - 2 * top))
    shape = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    shape.alpha_composite(panel.resize((w, h), Image.LANCZOS), ((width - w) // 2, (height - h) // 2))
    neutral = max(paper[:3]) - min(paper[:3]) <= 3
    blocks = b""
    for size in mip_sizes(width, height, mips):
        level = work / "level.dds"
        mip = shape.resize(size, Image.LANCZOS)
        mip.save(level, pixel_format="DXT5")
        data = level.read_bytes()
        encoded = data[header_size(data):]
        if neutral:   # owner 2026-11-13 (option c): no green cast, no 3-band fade
            encoded = neutral_grey_colour(encoded, mip.convert("L"), dither=True)
        blocks += encoded
    if len(blocks) != len(stock) - header_size(stock):
        raise SkipAsset("popup_panel: encoded size differs from the stock texture")
    return stock[:header_size(stock)] + blocks


POPUP_LAUREL_BANDS = (0.075, 0.915)   # T_UI_BackgroundHorizontal: laurels in rows < 7.5% and > 91.5% (local-contrast scan, RMI-096)


def popup_paper(stock_dds, dark, light, laurel, strength, work):
    """T_UI_BackgroundHorizontal for the swapped popups (RMI-096): the paper's grain on the parchment curve (2nd percentile ..
    median brightness -> dark..light, neutral); inside the top and bottom laurel bands, opaque pixels brighter than their
    surroundings (8-pixel box blur) blend toward the laurel colour by that local contrast, times strength (the paper's lighter
    middle is not a laurel); alpha (the torn edges) kept (BC3 re-encoded with the stock alpha)."""
    from PIL import ImageFilter
    image = decode_rgba(stock_dds)
    width, height = image.size
    pixels = list(image.get_flattened_data())
    grey = Image.new("L", image.size)
    grey.putdata([round(lum(p)) for p in pixels])
    local = list(grey.filter(ImageFilter.BoxBlur(8)).get_flattened_data())
    # the paper behind the laurels: a smoothed 9x9 minimum (the leaves are lighter than the band they are painted on)
    behind = list(grey.filter(ImageFilter.MinFilter(9)).filter(ImageFilter.BoxBlur(4)).get_flattened_data())
    values = sorted(lum(p) for p in pixels if p[3] > 128) or [0, 255]
    low, mid = values[len(values) * 2 // 100], values[len(values) // 2]
    top, bottom = POPUP_LAUREL_BANDS[0] * height, POPUP_LAUREL_BANDS[1] * height
    ramp = max(1.0, min(16.0, top / 3))   # 16 rows on the stock 1024-row paper
    out = []
    for i, p in enumerate(pixels):
        v = lum(p)
        y = i // width
        in_band = p[3] >= 250 and (y < top or y > bottom)
        # how far inside a band (0 at its inner edge -> 1 after `ramp` rows): no visible step where the band ends
        depth = min(1.0, max(top - y, y - bottom, 0) / ramp) if in_band else 0.0
        base = (parchment_curve(min(v, mid), low, mid, dark, light) * (1 - depth)
                + parchment_curve(min(behind[i], mid), low, mid, dark, light) * depth)
        t = 0.0
        if in_band:
            t = min(1.0, max(0.0, (v - local[i] - 3) / 14)) * strength
        out.append(tuple(round(base * (1 - t) + c * t) for c in laurel) + (p[3],))
    result = Image.new("RGBA", image.size)
    result.putdata(out)
    return colour_keep_alpha(stock_dds, result, work)


def laurel_tint(stock_dds, rgb):
    """The laurel colour scaled by each colour's brightness over the texture's 98th-percentile brightness (opaque pixels): the
    brightest part lands on rgb, darker parts stay darker, black stays black (the leaf border's background); BC3/BC1 colour
    endpoints mapped (indices and alpha kept). RMI-096, owner 2026-11-13: all laurels grouped and blue."""
    values = sorted(lum(p) for p in decode_rgba(stock_dds).get_flattened_data() if p[3] > 128) or [255]
    high = values[len(values) * 98 // 100] or 255
    return map_colours(stock_dds.read_bytes(), lambda c: tuple(min(255.0, x * lum(c) / high) for x in rgb))


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


# tab_paper (owner decisions 34-35, 2026-11-12; fitted to the owner's Near Black reference, KB custom-pack-builder-plan.md
# "Owner style requests during 28a"): the stock Inventory/Magic tab atlas recoloured, stock alpha kept.
TAB_PAPER_LUM = (80.0, 210.0)            # stock Rec. 601 luminance that maps to tabPaperDark / tabPaperLight (line extended)
TAB_ICON, TAB_ICON_TOP, TAB_ICON_SHIFT = 76, 12, -2   # the clean T_Category_* icon over each stock tab icon
TAB_GLYPH_BOX = (-42, 36, 12, 88)        # glyph box around each centre: x from, x to, y from, y to
TAB_ARROW_BOX = (16, 36, 63)             # arrow box: |dx| <= 16 from each arrow centre, y from, y to
TAB_WARMTH_INSIDE, TAB_WARMTH_NEAR, TAB_WARMTH_RAMP, TAB_NEAR = 0.22, 0.24, 0.06, 3


def lum601(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


# RMI-088: BC3 stores colour as RGB565 and PIL's encoder truncates (red/blue keep v >> 3, green v >> 2), so a dark grey between
# the red/blue levels decodes green ((24, 28, 24) for 29). Every channel goes on the 5-bit levels (green too, so a grey stays
# grey), dithered 4x4 between the two nearest levels to keep the mean and the gradient.
LEVELS5 = [(k << 3) | (k >> 2) for k in range(32)]
BAYER4 = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)


def dither565(value, x, y):
    v = max(0.0, min(255.0, value))
    k = min(30, max(0, int(v // 8.2258)))
    while k < 30 and LEVELS5[k + 1] <= v:
        k += 1
    while k > 0 and LEVELS5[k] > v:
        k -= 1
    low, high = LEVELS5[k], LEVELS5[k + 1]
    return high if (v - low) / (high - low) > (BAYER4[(y % 4) * 4 + x % 4] + 0.5) / 16 else low


def tab_paper(stock_dds, v, work, stock):
    """Paper = tabPaperDark..tabPaperLight along the stock luminance; brown strokes (warmth (R - B) / luminance over a ramp)
    become tabGlyph inside the clean icon outline (start 0.22), within 3 px of it and in the arrow boxes (start 0.24), nowhere
    else. stock(name) gives the clean T_Category_* masks (Inventory for T_Tab_*, Magic for T_Magic_Tabs_*)."""
    from PIL import ImageFilter
    image = Image.open(stock_dds).convert("RGBA")
    width, height = image.size
    names = MAGIC_TAB_MASKS if Path(stock_dds).stem.lower().startswith("t_magic_tabs") else TAB_MASKS
    if stock is None or len(names) != len(tabs.CENTERS):
        raise SkipAsset("tab_paper needs the five clean category masks")
    inside = Image.new("L", (width, height))
    for centre, name in zip(tabs.CENTERS, names):
        mask = Image.open(stock(name)).convert("RGBA").getchannel("A").resize((TAB_ICON, TAB_ICON), Image.BILINEAR)
        inside.paste(mask.point(lambda a: 255 if a > 128 else 0), (centre + TAB_ICON_SHIFT - TAB_ICON // 2, TAB_ICON_TOP))
    near = inside.filter(ImageFilter.MaxFilter(2 * TAB_NEAR + 1))
    inside_px, near_px = inside.load(), near.load()
    dark, light = v["tabPaperDark"], v["tabPaperLight"]
    glyph = v["tabGlyph"]
    low, high = TAB_PAPER_LUM
    gx0, gx1, gy0, gy1 = TAB_GLYPH_BOX
    adx, ay0, ay1 = TAB_ARROW_BOX
    src, out = image.load(), Image.new("RGBA", image.size)
    dst = out.load()
    for y in range(height):
        for x in range(width):
            p = src[x, y]
            l = lum601(p)
            t = (l - low) / (high - low)
            paper = [dark[i] + (light[i] - dark[i]) * t for i in range(3)]
            start = None
            if gy0 <= y <= gy1 and any(gx0 <= x - c <= gx1 for c in tabs.CENTERS):
                start = TAB_WARMTH_INSIDE if inside_px[x, y] else (TAB_WARMTH_NEAR if near_px[x, y] else None)
            if start is None and ay0 <= y <= ay1 and any(abs(x - c) <= adx for c in tabs.ARROWS):
                start = TAB_WARMTH_NEAR
            w = 0.0
            if start is not None and l > 0:
                w = min(1.0, max(0.0, ((p[0] - p[2]) / l - start) / TAB_WARMTH_RAMP))
            dst[x, y] = (*(dither565(paper[i] + (glyph[i] - paper[i]) * w, x, y) for i in range(3)), 255)
    return splice_colour(stock_dds, out, work)


def charcoal_texture(stock_dds, preset):
    """charcoal:<preset> (owner recipe 2026-11-13, tools/pack_builder/charcoal_tabs.py): a stock parchment tab texture recoloured
    pixel by pixel to dark charcoal parchment with light icons (fixed owner palette); written as neutral greys into the stock
    blocks: BC3 colour halves (alpha halves byte for byte, no dither), BC1 four-colour blocks (refuses punch-through)."""
    if preset not in charcoal_tabs.PRESETS:
        raise SkipAsset(f"unknown charcoal preset {preset!r}")
    data = stock_dds.read_bytes()
    if int.from_bytes(data[28:32], "little") > 1:
        raise SkipAsset("charcoal recolours one mip level; the stock texture has more")
    grey, _ = charcoal_tabs.recolour(decode_rgba(stock_dds), **charcoal_tabs.PRESETS[preset])
    bc1 = bc1_header_size(data)
    if bc1:
        try:
            return data[:bc1] + charcoal_tabs.neutral_grey_bc1(data[bc1:], grey)
        except ValueError as error:
            raise SkipAsset(str(error))
    head = header_size(data)
    return data[:head] + neutral_grey_colour(data[head:], grey, dither=False)


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


def neutral(data, rgb, body):
    """The art made neutral grey, its brightness scaled so the stock body brightness `body` lands on the colour's brightness:
    each BC3 colour endpoint becomes grey(min(255, lum * lum(rgb) / body)); indices and alpha blocks kept (shape and relief)."""
    out = bytearray(data)
    target = lum(rgb)

    def endpoint_lum(e):
        return lum((((e >> 11) & 31) * 255 / 31, ((e >> 5) & 63) * 255 / 63, (e & 31) * 255 / 31))

    for offset in range(header_size(data) + 8, len(out), 16):
        for at in (offset, offset + 2):
            grey = min(255.0, endpoint_lum(int.from_bytes(out[at:at + 2], "little")) * target / body)
            out[at:at + 2] = rgb565((grey, grey, grey)).to_bytes(2, "little")
    return bytes(out)


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
    return colour_keep_alpha(stock_dds, Image.open(ART / (name + ".png")).convert("RGBA"), work)


def stolen_mark(stock_dds, rgb, work):
    """T_Icon_stolen: our stolen shape (menus/Stolen-Item.png, the white copy the plugin used to swap in, cropped to fill the width) in one colour,
    at the stock size, every mip level BC3-encoded; replaces the stock art and its alpha (like enchanted_dot)."""
    stock = stock_dds.read_bytes()
    height, width = int.from_bytes(stock[12:16], "little"), int.from_bytes(stock[16:20], "little")
    mips = max(1, int.from_bytes(stock[28:32], "little"))
    shape = Image.open(Path(MENUS) / "Stolen-Item.png").convert("RGBA")
    # owner 2026-11-13: the row mark 80% of the sort-bar icon. The row box is 16 units and the toggle draws the shape ~20.6
    # wide (probe MRROW), so the shape is cropped to its outline and fills the texture width (~78%, the most a 16 box holds).
    box = shape.getchannel("A").getbbox()
    shape = shape.crop(box)
    side = max(shape.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(shape, ((side - shape.width) // 2, (side - shape.height) // 2))
    shape = square
    colour = tuple(round(c) for c in rgb[:3])
    blocks = bytearray()
    for size in mip_sizes(width, height, mips):
        alpha = shape.getchannel("A").resize(size, Image.LANCZOS)
        level_image = Image.new("RGBA", size, colour + (0,))
        level_image.putalpha(alpha)
        level = work / "level.dds"
        level_image.save(level, pixel_format="DXT5")
        data = level.read_bytes()
        blocks += data[header_size(data):]
    assert len(blocks) == len(stock) - header_size(stock), "size mismatch"
    return stock[:header_size(stock)] + bytes(blocks)


def colour_keep_alpha(stock_dds, art, work):
    """A colour image (any size) into a stock BC3 texture: every mip level BC3-encoded by Pillow, only the colour halves of the
    blocks taken, so the stock alpha stays byte for byte."""
    stock = stock_dds.read_bytes()
    height, width = int.from_bytes(stock[12:16], "little"), int.from_bytes(stock[16:20], "little")
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


def literal_rgba(value):
    """A colour-file value as a linear RGBA float4: one number = a linear grey ([Skin] scrollbar entries), four = a linear
    [Skin] tint (phase 28b: ornament, ring, emphasisInk), three = sRGB 0-255."""
    if isinstance(value, (int, float)):
        return (float(value),) * 3 + (1.0,)
    if len(value) == 4:
        return tuple(float(c) for c in value)
    return (*(linear(c) for c in value), 1.0)


def grey_scale(data, k):
    """The art's own brightness as a neutral grey times k (clamped at white): the run-time swap art of make_dark_skin_art.py
    gray(k) (the Effects/skill title band 0.45, sigil band 0.45, sigil icons 2.2/1.8, ring centre 1.0; decision 28 phase 28b).
    BC3/BC1 colour blocks re-encoded in neutral greys (grey_blocks), an uncompressed texture per pixel; alpha kept."""
    if bgra_layout(data):
        return map_colours(data, lambda c: (min(255.0, lum(c) * k),) * 3)
    return grey_blocks(data, lambda v: min(255.0, v * k))


def skin_flat(data, base, tint):
    """One flat colour = the sRGB grey base times a linear [Skin] tint (the description rules: the white RowSeparator art, 235,
    drawn in the ornament tint at run time before phase 28b); alpha kept (BC3 or uncompressed)."""
    return flat_blocks(data, [srgb(linear(base) * t) for t in tint[:3]])


# fixed literal colours a data row can name instead of a colour-file key (the Focusbox equip tint: white, as the run-time
# swap drew it; decision 28)
CONSTANT_COLOURS = {"white": [255.0, 255.0, 255.0]}


def bp_colour(uexp, package, values):
    """A blueprint's or material's colour literals set to the colour-file values (linear floats, the stock alpha kept: the
    button hover fill and rules are 0.62-0.70, RMI-087); refuses an unexpected stock value (a game update moved it): nothing
    is patched then."""
    out = bytearray(uexp)
    for offset, expected, name in BLUEPRINT_COLOURS[package]:
        found = struct.unpack_from("<4f", uexp, offset) if len(uexp) >= offset + 16 else ()
        if len(found) != 4 or any(abs(a - b) > 5e-4 for a, b in zip(found, expected)):
            raise SkipAsset(f"{package}: stock colour {found} at byte {offset}, expected {expected} (game update? re-scan the asset)")
        value = CONSTANT_COLOURS[name] if name in CONSTANT_COLOURS else values[name]
        scale = BLUEPRINT_SCALES.get((package, offset))
        if scale is not None:   # an sRGB colour brightened (RMI-091: the container lines in the lighter accent)
            value = [min(255.0, float(c) * scale) for c in value[:3]]
        struct.pack_into("<4f", out, offset, *literal_rgba(value)[:3], found[3])
    for row in BLUEPRINT_LITERALS.get(package, []):   # fixed literals (RMI-089), stock-checked like the colours
        fmt = "<d" if row.get("type") == "double" else "<f"
        width = struct.calcsize(fmt)
        found = struct.unpack_from(fmt, uexp, row["offset"])[0] if len(uexp) >= row["offset"] + width else None
        if found is None or abs(found - row["stock"]) > 1e-6:
            raise SkipAsset(f"{package}: stock value {found} at byte {row['offset']} ({row['widget']}), expected {row['stock']} "
                            f"(game update? re-scan the asset)")
        struct.pack_into(fmt, out, row["offset"], float(row["value"]))
    return bytes(out)


def preview_frame(stock_dds, v, work):
    """The stock brightness inverted between previewFrameLight (black) and previewFrameDark (white)."""
    dark, light = v["previewFrameDark"], v["previewFrameLight"]
    return per_pixel(stock_dds, work, lambda p: tuple(
        round(dark[i]) + (round(light[i]) - round(dark[i])) * (255 - round(lum(p))) // 255 for i in range(3)))


def recipe(name):
    """recipe name -> (number elements, colour elements, function(stock dds, values, work, stock) -> DDS bytes)"""
    kind, _, arg = name.partition(":")
    if kind == "paper_scale":   # paper_scale[:<factor>]: the popup paper's light end = parchmentLight x factor (RMI-096)
        factor = float(arg) if arg else 1.0
        return (), ("parchmentLight",), lambda dds, v, work, stock: paper_scale(dds, v["parchmentLight"][0] * factor)
    if kind == "popup_panel":   # popup_panel:<side>:<top>: the swapped-in popup paper as a framed panel (RMI-096, mock-up D)
        side, _, top = arg.partition(":")
        return (), ("parchmentLight",), lambda dds, v, work, stock: popup_panel(
            dds, v["parchmentLight"], float(side or 0), float(top or 0), work)
    if kind == "popup_paper":   # popup_paper:<laurel strength>: the swapped-in popup paper (RMI-096); 0 = no laurels
        strength = float(arg) if arg else 0.6
        if strength == 0:   # owner 2026-11-13: "discard the laurels here"
            return (), ("parchmentDark", "parchmentLight"), lambda dds, v, work, stock: popup_paper(
                dds, v["parchmentDark"][0], v["parchmentLight"][0], (0, 0, 0), 0.0, work)
        return (), ("parchmentDark", "parchmentLight", "pattern"), lambda dds, v, work, stock: popup_paper(
            dds, v["parchmentDark"][0], v["parchmentLight"][0], v["pattern"], strength, work)
    if kind == "mask_peak":   # mask_peak:<value>: a shader mask texture's brightest part scaled to <value> (RMI-096: the popup
        # laurel border is the material's R-channel mask, painted white by the shader; owner "very faint")
        return (), (), lambda dds, v, work, stock: paper_scale(dds, float(arg))
    if kind == "smooth_grain":   # smooth_grain:<blur>:<amount> (RMI-091: the container panel, on the shared paper tone)
        blur, _, amount = arg.partition(":")
        return (), ("parchmentDark", "parchmentLight", "panel"), lambda dds, v, work, stock: smooth_grain(
            dds, v["parchmentDark"][0], v["parchmentLight"][0], float(blur), float(amount),
            paper_tone(v["parchmentDark"][0], v["parchmentLight"][0], v["panel"][0]))
    if kind == "page_grain":   # page_grain:<blur>:<grain +-> (RMI-091: the Inventory page, on the shared paper tone)
        blur, _, amount = arg.partition(":")
        return (), ("frame", "panel", "parchmentDark", "parchmentLight"), lambda dds, v, work, stock: page_grain(
            dds, v["frame"][0], v["panel"][0], float(blur), float(amount),
            paper_tone(v["parchmentDark"][0], v["parchmentLight"][0], v["panel"][0]))
    if kind == "paper_invert":   # paper_invert:<blur> (RMI-091 round 3: the standard parchment look, container panel)
        return (), ("parchmentDark", "parchmentLight", "panel"), lambda dds, v, work, stock: invert_paper(
            dds, paper_tone(v["parchmentDark"][0], v["parchmentLight"][0], v["panel"][0]), float(arg))
    if kind == "page_invert":   # page_invert:<blur> (RMI-091 round 3: the Inventory and Magic pages, frame relief kept)
        return (), ("frame", "panel", "parchmentDark", "parchmentLight"), lambda dds, v, work, stock: invert_paper(
            dds, paper_tone(v["parchmentDark"][0], v["parchmentLight"][0], v["panel"][0]), float(arg), "frame",
            v["frame"][0], v["panel"][0])
    if kind == "details_invert":   # details_invert:<blur> (RMI-091 round 3: the details page, its own tone)
        return ("detailsPage",), (), lambda dds, v, work, stock: invert_paper(
            dds, details_tone(dds, v["detailsPage"]), float(arg), "shadow")
    if kind == "band_lines":   # band_lines:<scale> (RMI-091: the thin lines around the container name)
        return (), ("pattern",), lambda dds, v, work, stock: band_lines(dds, v["pattern"], float(arg))
    if kind == "details_smooth":   # details_smooth:<blur>:<amount> (RMI-091: the Inventory details page)
        blur, _, amount = arg.partition(":")
        return ("detailsPage",), (), lambda dds, v, work, stock: details_smooth(dds, v["detailsPage"], float(blur), float(amount))
    if kind == "gray_smooth":   # gray_smooth:<blur> (RMI-091: the Inventory preview box)
        return (), (), lambda dds, v, work, stock: gray_smooth(dds, float(arg))
    if kind == "line_tint":   # line_tint:<scale> (RMI-091: the glow lines around the container name)
        return (), ("pattern",), lambda dds, v, work, stock: line_tint(dds, v["pattern"], float(arg))
    if kind == "laurel_tint":   # the popup title flowers and leaf border in the laurel colour (RMI-096 Laurels group)
        return (), ("pattern",), lambda dds, v, work, stock: laurel_tint(dds, v["pattern"])
    if kind == "charcoal":   # charcoal[:<preset>]: the charcoal tab recolour (palette fixed, no colour element)
        return (), (), lambda dds, v, work, stock: charcoal_texture(dds, arg)
    if kind == "flat":
        return (), (arg,), lambda dds, v, work, stock: flat_blocks(dds.read_bytes(), v[arg])
    if kind == "flat_alpha":   # flat_alpha:<key>:<alpha scale>
        key, _, scale = arg.partition(":")
        return (), (key,), lambda dds, v, work, stock: flat_alpha(dds.read_bytes(), v[key], float(scale), dds)
    if name == "clear":
        return (), (), lambda dds, v, work, stock: clear_alpha(dds.read_bytes())
    if kind == "grey_scale":   # grey_scale:<factor>
        return (), (), lambda dds, v, work, stock: grey_scale(dds.read_bytes(), float(arg))
    if kind == "skin_flat":   # skin_flat:<[Skin] name>:<sRGB grey base>
        key, _, base = arg.partition(":")
        return (), (f"Skin.{key}",), lambda dds, v, work, stock: skin_flat(dds.read_bytes(), float(base), v[f"Skin.{key}"])
    if kind == "tone":
        return (), (arg,), lambda dds, v, work, stock: tone(dds.read_bytes(), v[arg])
    if kind == "neutral":   # neutral:<key>:<stock body brightness> (RMI-095: the Empty label band)
        key, _, body = arg.partition(":")
        return (), (key,), lambda dds, v, work, stock: neutral(dds.read_bytes(), v[key], float(body))
    if kind in ("bp_colour", "material_colour"):   # a widget blueprint or a material: build() patches its .uexp (no DDS)
        names = tuple(sorted({name for _, _, name in BLUEPRINT_COLOURS[arg]} - set(CONSTANT_COLOURS)))
        return (), names, lambda uexp, v, work, stock: bp_colour(uexp, arg, v)
    if kind == "tab":
        return TAB_STYLE, (), lambda dds, v, work, stock: tab_atlas(dds, int(arg), v, work, stock)
    if kind == "magic_tab":
        return TAB_STYLE, (), lambda dds, v, work, stock: magic_tab_atlas(dds, int(arg), v, work, stock)
    if kind == "altar_metal":
        return (), (arg,), lambda dds, v, work, stock: map_colours(dds.read_bytes(), lambda c: altar_metal(c, v[arg]))
    if kind == "altar_panel":
        def panel(dds, v, work, stock):
            name, channel = PANEL_MASKS.get(Path(dds).stem, (None, 0))
            mask = (stock(name), channel) if name and stock else None
            return altar_panel_texture(dds, v[arg], v["altarPaper"], work, mask)
        return (), (arg, "altarPaper"), panel
    if name == "parchment:enchanting":
        # Match the darker left-page reference using the existing parchment controls.
        return (), ("parchmentDark", "parchmentLight"), lambda dds, v, work, stock: parchment(
            dds, v["parchmentDark"][0] * 0.60, v["parchmentLight"][0] * 0.82)
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
        "tab_strip": ((), ("enchantingTabIcon", "enchantingTabPaper"), lambda dds, v, work, stock: tab_strip_texture(
            dds, v["enchantingTabIcon"], v["enchantingTabPaper"], work, stock)),
        "tab_paper": ((), ("tabPaperDark", "tabPaperLight", "tabGlyph"), tab_paper),
        "png": ((), (), lambda dds, v, work, stock: owner_png(dds, dds.stem, work)),   # development only (owner art)
        "enchanted_dot": ((), ("enchantedDot",), lambda dds, v, work, stock: enchanted_dot(dds.read_bytes(), v["enchantedDot"])),
        "stolen_mark": ((), ("stolenMark",), lambda dds, v, work, stock: stolen_mark(dds, v["stolenMark"], work)),
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


def needed_names(colours=True, layout=False):
    """Every name the build extracts: the data files' assets and the glyph masks (colours), the layout packages (layout)."""
    names = set()
    if colours:
        names |= ({Path(a).name for a in TEXTURES} | {n for names in GLYPH_MASKS.values() for n in names}
                  | {mask for mask, _ in PANEL_MASKS.values()})
    if layout:
        names |= set(LAYOUT)
    return sorted(names)


def extract_all(args, folder, colours=True, layout=False):
    """One retoc to-legacy call with one -f per needed name into folder; refuses any colour asset it did not extract (a wrong
    folder or a game update: nothing is built; a missing layout package is skipped by its own check). Returns {lower-case name:
    sorted .uasset paths}."""
    cmd = [args.retoc, "to-legacy", "--no-shaders", "--version", "UE5_3"]
    for name in needed_names(colours, layout):
        cmd += ["-f", name]
    run(cmd + [args.paks, folder])
    found = {}
    for path in sorted(Path(folder).rglob("*.uasset")):
        found.setdefault(path.stem.lower(), []).append(path)
    content = Path(folder) / "OblivionRemastered/Content"
    missing = [a for a in TEXTURES if colours and not (content / a).with_suffix(".uasset").is_file()]
    missing += [n for names in GLYPH_MASKS.values() for n in names if colours and n.lower() not in found]
    if missing:
        raise BuildError(f"retoc did not extract {len(missing)} asset(s) from {args.paks} (game update or wrong folder?):\n  "
                         + "\n  ".join(missing))
    return found


def checked_texture(stock, baked):
    """Refuses a recipe result the game would not take: not the stock size or header (dimensions, mips, format)."""
    if len(baked) != len(stock):
        raise SkipAsset(f"the recipe's result has another size ({len(baked)} bytes, stock texture {len(stock)})")
    head = texture_header(stock)
    if baked[:head] != stock[:head]:
        raise SkipAsset("the recipe changed the texture header (size, mips or format)")
    return baked


def format_supported(kind, stock):
    """Whether a recipe kind can recolour this stock DDS: every recipe takes BC3; parchment, altar_metal, altar_panel and
    tab_strip also BC1 and uncompressed 32-bit; flat also uncompressed 32-bit (the altar separators, RMI-087)."""
    if kind == "charcoal" and bc1_header_size(stock):   # the two BC1 Enchanting strips
        return True
    try:
        header_size(stock)
        return True
    except AssertionError:
        pass
    if kind == "paper_scale" and r8_header_size(stock):   # the popup paper (PF_G8, RMI-096)
        return True
    if kind in ("parchment", "paper_scale", "mask_peak", "laurel_tint", "altar_metal", "altar_panel", "tab_strip", "grey_scale"):
        return bool(bc1_header_size(stock) or bgra_layout(stock))
    return kind in ("flat", "skin_flat") and bool(bgra_layout(stock))


def bake(asset, recipe_name, colours, art, legacy, stage, work, stock_dds, inject, sizes=None):
    """One asset into the stage folder: (values it read, manifest entry). Raises SkipAsset before writing when a check fails."""
    name = Path(asset).name
    kind = recipe_name.partition(":")[0]
    make = recipe(recipe_name)[2]
    layout_error = None
    values = recipe_values(recipe_name, colours)
    source = legacy / "OblivionRemastered/Content" / asset
    target = stage / "OblivionRemastered/Content" / asset
    if kind == "laurels" and sha256(source.with_suffix(".uexp")) != LAUREL_STOCK_SHA256:
        raise SkipAsset("laurel stock asset differs from the verified game build")
    if kind == "alchemy" and sha256(source.with_suffix(".uexp")) != ALCHEMY_STOCK_SHA256.get(name):
        raise SkipAsset(f"{name}: stock asset differs from the verified game build (game update?)")
    if kind == "png" and not (art / (name + ".png")).is_file():
        raise SkipAsset(f"owner art missing ({name}.png in {art}); the game's stock texture shows")
    if kind in ("rich_row", "bp_colour", "material_colour"):   # a data table, blueprint or material: its .uexp is patched (no DDS)
        uexp = make(source.with_suffix(".uexp").read_bytes(), values, work, None)
        uasset = source.with_suffix(".uasset").read_bytes()
        if sizes is not None and name in LAYOUT:   # the same package also carries sizes (decision 46): both in one copy
            try:
                uasset, uexp = layout_package(uasset, uexp, name, sizes)
            except SkipAsset as error:   # the colours still bake; the sizes of this package are listed as skipped
                layout_error = str(error)
        if name in IMPORT_SWAPS:   # RMI-096: the popup paper draws a plain texture instead of the shader material
            uasset = swap_imports(uasset, IMPORT_SWAPS[name])
        target.parent.mkdir(parents=True, exist_ok=True)
        target.with_suffix(".uasset").write_bytes(uasset)
        target.with_suffix(".uexp").write_bytes(uexp)
    else:
        stock_path = stock_dds(name)
        stock = stock_path.read_bytes()
        if not format_supported(kind, stock):
            raise SkipAsset("the stock texture is not BC3 (game update?)")
        dds = work / "recoloured" / (name + ".dds")
        dds.parent.mkdir(parents=True, exist_ok=True)
        dds.write_bytes(checked_texture(stock, make(stock_path, values, work, stock_dds)))
        target.parent.mkdir(parents=True, exist_ok=True)
        exts = (".uasset", ".uexp") + ((".ubulk",) if source.with_suffix(".ubulk").is_file() else ())   # bulk mips (T_Effects_icon)
        for ext in exts:
            shutil.copy2(source.with_suffix(ext), target.with_suffix(ext))
        injected = work / "injected" / (name + ".uasset")
        inject(target.with_suffix(".uasset"), dds, injected)
        for ext in (".uasset", ".uexp", ".ubulk"):
            if injected.with_suffix(ext).is_file():
                shutil.copy2(injected.with_suffix(ext), target.with_suffix(ext))
    layout = {}
    if sizes is not None and name in LAYOUT and kind == "bp_colour":
        layout = {"layout_skipped": layout_error} if layout_error else {"layout": True}
    return values, {"recipe": recipe_name, "style": sorted(values), **layout,
                    "stock_uexp_sha256": sha256(source.with_suffix(".uexp")),
                    "baked_uexp_sha256": sha256(target.with_suffix(".uexp"))}


def build(args, out, pack=DEFAULT_PACK, colours=None, art=None, log=print, sizes=None):
    """Bake every asset of the data files with the colour values (default: the Dark values) into <out>/<pack>.*; an asset that
    fails its check is skipped and listed. sizes ([Sizes] values, Step B decision 46): the layout packages are baked into the
    same pack (a colour-patched one gets both); None = colours only. args: paks, retoc, ddstools (None = the frozen copy).
    Returns the manifest."""
    global ART
    saved, ART = ART, Path(ART if art is None else art)   # the png recipe (owner_png) reads ART
    try:
        return _build(args, out, pack, colours, ART, log, sizes)
    finally:
        ART = saved


def stage_layout(package, legacy, stage, sizes):
    """One layout-only package into the stage folder (manifest entry); SkipAsset before writing when a check fails."""
    row = LAYOUT[package]
    source = legacy / "OblivionRemastered/Content" / row["asset"]
    if not source.with_suffix(".uasset").is_file():
        raise SkipAsset("not extracted from the game (game update or wrong folder?)")
    uasset, uexp = layout_package(source.with_suffix(".uasset").read_bytes(), source.with_suffix(".uexp").read_bytes(), package, sizes)
    target = stage / "OblivionRemastered/Content" / row["asset"]
    target.parent.mkdir(parents=True, exist_ok=True)
    target.with_suffix(".uasset").write_bytes(uasset)
    target.with_suffix(".uexp").write_bytes(uexp)
    return {"recipe": "layout", "stock_uexp_sha256": sha256(source.with_suffix(".uexp")),
            "baked_uexp_sha256": sha256(target.with_suffix(".uexp"))}


def layout_entries(legacy, stage, sizes, skip_names, textures, skipped, log):
    """Every layout package not already staged by a colour recipe: staged, or skipped and listed."""
    for package, row in LAYOUT.items():
        if package in skip_names:
            continue
        try:
            textures[row["asset"]] = stage_layout(package, legacy, stage, sizes)
            log(f"{package}: layout")
        except SkipAsset as error:
            skipped.append({"asset": row["asset"], "recipe": "layout", "reason": str(error)})
            log(f"SKIPPED {package}: {error}")


def used_sizes(sizes):
    return {k: float(v) for k, v in sizes.items() if k not in custom_colours.TEXT_ROLES}


def build_layout(args, out, sizes, pack=LAYOUT_PACK, log=print):
    """MRLayout_P: the layout packages only, with the sizes (decision 46: installed when the colour pack is not). Returns the
    manifest ("sizes": what it baked; the builder compares it with [Sizes])."""
    pack = pack_name(pack)
    log = log or (lambda line: None)
    textures, skipped = {}, []
    if not Path(args.retoc).is_file():
        raise BuildError(f"retoc not found: {args.retoc} (--retoc: the path of retoc.exe)")
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        stage, legacy = work / "stage", work / "legacy"
        log(f"Reading {len(LAYOUT)} layout packages from {args.paks}")
        extract_all(args, legacy, colours=False, layout=True)
        layout_entries(legacy, stage, sizes, set(), textures, skipped, log)
        if not textures:
            raise BuildError(f"no layout package passed its check ({len(skipped)} skipped): nothing to build")
        out.mkdir(parents=True, exist_ok=True)
        log("Writing the layout pack")
        run([args.retoc, "to-zen", "--version", "UE5_3", stage, out / f"{pack}.utoc"])
    return write_manifest(out, pack, {"sizes": used_sizes(sizes), "textures": textures, "skipped": skipped})


def write_manifest(out, pack, body):
    files = {}
    for ext in ("pak", "utoc", "ucas"):
        path = out / f"{pack}.{ext}"
        if not path.is_file():
            raise BuildError(f"retoc wrote no {path.name}")
        files[path.name] = sha256(path)
    manifest = {"pack": pack, "game_build": GAME_BUILD,
                "built_by": paths.EXE_NAME if paths.is_frozen() else "tools/make_dark_pack.py",
                "builder_version": VERSION, **body, "files": files}
    (out / f"{pack}.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def _build(args, out, pack, colours, art, log, sizes=None):
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
        log(f"Reading {len(needed_names(True, sizes is not None))} stock assets from {args.paks}")
        found = extract_all(args, legacy, colours=True, layout=sizes is not None)
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
                values, entry = bake(asset, recipe_name, colours, art, legacy, stage, work, stock_dds, inject, sizes)
            except (BuildError, KeyboardInterrupt):
                raise
            except Exception as error:   # SkipAsset or a recipe failing on unexpected data: skip and list, build the rest
                reason = str(error) if isinstance(error, SkipAsset) else f"{type(error).__name__}: {error}"
                gc.collect()   # a failed in-process inject can still hold the staged file open (Windows: WinError 32)
                for ext in (".uasset", ".uexp", ".ubulk"):
                    try:
                        target.with_suffix(ext).unlink(missing_ok=True)
                    except OSError as locked:
                        log(f"could not remove the staged {target.with_suffix(ext).name}: {locked}")
                skipped.append({"asset": asset, "recipe": recipe_name, "reason": reason})
                log(f"SKIPPED {name}: {reason}")
                continue
            style.update(values)
            textures[asset] = entry
            if entry.get("layout_skipped"):
                skipped.append({"asset": asset, "recipe": "layout", "reason": entry["layout_skipped"]})
                log(f"SKIPPED {name} sizes: {entry['layout_skipped']}")
            log(f"{name}: {recipe_name} {values}")
        if sizes is not None:   # the layout packages no colour recipe staged (a skipped colour blueprint is staged with sizes only)
            staged = {Path(a).name for a in textures}
            layout_entries(legacy, stage, sizes, staged, textures, skipped, log)
        if not textures:
            raise BuildError(f"no asset passed its check ({len(skipped)} skipped): nothing to build")
        out.mkdir(parents=True, exist_ok=True)
        log("Writing the pack")
        run([args.retoc, "to-zen", "--version", "UE5_3", stage, out / f"{pack}.utoc"])
    body = {"style": style, "textures": textures, "skipped": skipped}
    if sizes is not None:
        body["sizes"] = used_sizes(sizes)
    return write_manifest(out, pack, body)


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
