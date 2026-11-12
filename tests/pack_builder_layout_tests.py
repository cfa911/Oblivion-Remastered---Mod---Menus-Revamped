"""Custom pack builder Step B (owner 2026-11-12, decisions 29, 31, 45, 46): the sizes are baked.
- tools/menus/layout.json: the compact rows (22 blueprints; the run-time code was removed by decision 45) as stock-checked float and
  double patches (recipe bp_float), each sized by a [Sizes] key or fixed; the three values the cook did not store are added by a
  package rewrite (KB techniques/unversioned-package-rewrite.md, proven in spike 29a);
- every Rows/Text option of the builder feeds at least one baked value;
- bp_float refuses an unexpected stock value (a game update): the package is skipped and listed, nothing half-written;
- one Build writes MRCustom_P = colours + every size and MRLayout_P = sizes only (decision 46: the plugin installs exactly one);
  a package in both the colour data and the layout data is staged once with both patches;
- [Layout] bLayoutPack=0 in MenusRevamped.ini (read by the builder too): MRCustom_P without sizes, no MRLayout_P.
retoc and the DDS tools are fakes (like pack_builder_build_tests.py)."""
import contextlib
import hashlib
import io
import json
import struct
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

import source_paths

fails = []
count = 0


def check(ok, why):
    global count
    count += 1
    if not ok:
        fails.append(why)


root = source_paths.ROOT
sys.path.insert(0, str(root / "tools"))
import custom_colours as cc  # noqa: E402
from pack_builder import pipeline as pipe  # noqa: E402

LAYOUT = getattr(pipe, "LAYOUT", {})
# the 22 blueprints of the run-time compact rows (removed by decision 45 once the bake was owner-accepted, 2026-11-12)
blueprints = ["WBP_ModernMenu_MagicEntry", "WBP_ModernMenu_MagicMenu", "WBP_OriginalMenu_InventoryIconEntry",
              "WBP_OriginalMenu_InventoryEntry", "WBP_OriginalMenu_InventoryMainPart", "WBP_ModernMenu_AlchemyItemEntry",
              "WBP_ModernMenu_AlchemyMenu", "WBP_ModernMenu_AlchemyFilters", "WBP_ModernMenu_FilterEffectEntry",
              "WBP_ModernMenu_ItemEffect", "WBP_ModernMenu_AlchemyItemsListView", "WBP_ModernMenu_EnchantmentEntry",
              "WBP_ModernMenu_EnchantmentMenu", "WBP_ModernMenu_SpellmakingEntry", "WBP_ModernMenu_SpellMakingMenu",
              "WBP_ModernMenu_SpellPurchaseItem", "WBP_ModernMenu_SpellPurchase", "WBP_ModernMenu_StatsSkillEntry",
              "WBP_ModernMenu_Stats_LevelDescription", "WBP_ModernMenu_StatsPage3", "WBP_ModernMenu_StatsPage3_Details",
              "WBP_Modern_QuestEntry"]

# ---- A. the data ----
check(sorted(LAYOUT) == sorted(blueprints), f"layout.json covers the 22 compact-row blueprints ({len(LAYOUT)})")
sized = set()
for package, row in LAYOUT.items():
    check(row.get("asset", "").endswith("/" + package), f"{package}: asset path ends with the package name")
    for f in row.get("floats", []):
        check(isinstance(f.get("offset"), int) and isinstance(f.get("stock"), float), f"{package} {f.get('widget')}: offset and stock")
        check(("size" in f) != ("value" in f), f"{package} {f.get('widget')}: either a [Sizes] key or a fixed value")
        if "size" in f:
            check(f["size"] in cc.SIZES and f["size"] not in cc.TEXT_ROLES, f"{package}: size key {f['size']} is a [Sizes] value")
            sized.add(f["size"])
rows_and_fonts = {k for k in cc.SIZES if k not in cc.TEXT_ROLES}
check(sized == rows_and_fonts, f"every Rows/Text option feeds a baked value (unused {sorted(rows_and_fonts - sized)})")
rewrites = {p: r["rewrites"] for p, r in LAYOUT.items() if r.get("rewrites")}
check(sorted(rewrites) == ["WBP_ModernMenu_AlchemyFilters", "WBP_ModernMenu_AlchemyItemsListView"],
      f"the two packages with unstored values have a rewrite ({sorted(rewrites)})")
check(sum(len(f) for r in rewrites.values() for f in [x["add"] for x in r]) == 3, "three unstored values are added")
check(sum(len(r.get("floats", [])) for r in LAYOUT.values()) == 75, "75 stored values (62 changes, squares and pairs counted)")

# ---- B. bp_float ----
bp_float = getattr(pipe, "bp_float", None)
check(callable(bp_float), "pipeline.bp_float exists")
if callable(bp_float) and "WBP_OriginalMenu_InventoryMainPart" in LAYOUT:
    pkg = "WBP_OriginalMenu_InventoryMainPart"
    floats = LAYOUT[pkg]["floats"]
    size = max(f["offset"] for f in floats) + 16
    stock = bytearray(size)
    for f in floats:
        if f.get("type") == "double":
            struct.pack_into("<d", stock, f["offset"], f["stock"])
        else:
            struct.pack_into("<f", stock, f["offset"], f["stock"])
    sizes = dict(cc.SIZES) | {"font.InventoryMainPart.inv_armor": 20.0}
    out = bp_float(bytes(stock), pkg, sizes)
    armor = [f for f in floats if f.get("size") == "font.InventoryMainPart.inv_armor"][0]
    spacer = [f for f in floats if f.get("type") == "double"][0]
    fixed = [f for f in floats if "value" in f and f.get("type") != "double"][0]
    check(struct.unpack_from("<f", out, armor["offset"])[0] == 20.0, "a sized value takes its [Sizes] value")
    check(struct.unpack_from("<d", out, spacer["offset"])[0] == spacer["value"], "a Spacer double takes its fixed value")
    check(abs(struct.unpack_from("<f", out, fixed["offset"])[0] - fixed["value"]) < 1e-3, "a fixed layout float takes its value")
    check(len(out) == len(stock), "an in-place patch keeps the size")
    bad = bytearray(stock)
    struct.pack_into("<f", bad, armor["offset"], 99.0)
    try:
        bp_float(bytes(bad), pkg, sizes)
        check(False, "an unexpected stock value is refused")
    except pipe.SkipAsset as error:
        check("stock" in str(error), "an unexpected stock value is refused with a SkipAsset naming the stock value")

# ---- C. the package rewrite ----
rewrite = getattr(pipe, "rewrite_package", None)
check(callable(rewrite), "pipeline.rewrite_package exists")
if callable(rewrite):
    tail = struct.pack("<f", 1.0) * 2 + bytes(4)
    export1 = b"\x80\x0b\x06" + b"\x00\x02\x01\x03" + struct.pack("<2f", 10.0, 10.0) + tail
    export2 = bytes(20)
    uexp = export1 + export2 + b"\xc1\x83\x2a\x9e"   # package tag
    base = 200
    uasset = bytearray(base)
    struct.pack_into("<q", uasset, 40, base + len(uexp) - 4)
    struct.pack_into("<qq", uasset, 120, len(export1), base)
    struct.pack_into("<qq", uasset, 160, len(export2), base + len(export1))
    desc = {"widget": "OverlaySlot_0", "export_start": 0, "export_size": len(export1), "header_pos": 3, "stock_fields": [0, 2],
            "stock_zeroes": [], "add": [[3, -75.0]], "field_sizes": [4, 4, 4, 4],
            "stock_export_sha256": hashlib.sha256(export1).hexdigest()}
    ua2, ux2 = rewrite(bytes(uasset), uexp, [desc])
    check(len(ux2) == len(uexp) + 4 and struct.unpack_from("<3f", ux2, 7) == (10.0, 10.0, -75.0),
          "the skipped Bottom is added after Left and Right (+4 bytes)")
    check(struct.unpack_from("<q", ua2, 120)[0] == len(export1) + 4 and struct.unpack_from("<q", ua2, 168)[0] == base + len(export1) + 4,
          "the export table follows: the export's size and the next export's offset")
    check(struct.unpack_from("<q", ua2, 40)[0] == base + len(ux2) - 4, "BulkDataStartOffset follows")
    check(ux2[-4:] == uexp[-4:], "the package tag is kept")
    try:
        rewrite(bytes(uasset), b"\x00" + uexp[1:], [desc])
        check(False, "a changed stock export is refused")
    except pipe.SkipAsset:
        check(True, "a changed stock export is refused (SkipAsset)")

# ---- D. the build: MRCustom_P with sizes, MRLayout_P sizes only ----
TEXTURES = pipe.TEXTURES
check(callable(getattr(pipe, "build_layout", None)), "pipeline.build_layout exists")


def layout_bytes(package):
    """A synthetic .uexp holding the layout stock values at their offsets and, for a colour-patched blueprint, its stock colour
    literals too (the rewrite exports are not the game's: those two packages are skipped by their stock check)."""
    row = LAYOUT.get(package, {})
    colours = pipe.BLUEPRINT_COLOURS.get(package, [])
    size = max([f["offset"] + 8 for f in row.get("floats", [])] + [o + 16 for o, _, _ in colours]
               + [r["export_start"] + r["export_size"] for r in row.get("rewrites", [])] + [16]) + 8
    data = bytearray(size)
    for f in row.get("floats", []):
        struct.pack_into("<d" if f.get("type") == "double" else "<f", data, f["offset"], f["stock"])
    for offset, stock, _ in colours:
        struct.pack_into("<4f", data, offset, *stock)
    return bytes(data)


def dds():
    head = b"DDS " + bytes(8) + (4).to_bytes(4, "little") + (4).to_bytes(4, "little") + bytes(64) + b"DXT5" + bytes(40)
    return head + bytes(16)


REAL_RECIPE = pipe.recipe


def fake_recipe(name):
    if name.partition(":")[0] in ("bp_colour", "material_colour", "rich_row"):
        return REAL_RECIPE(name)
    floats, rgbs, _ = REAL_RECIPE(name)
    return floats, rgbs, lambda data, values, work, stock: data.read_bytes()


class Fake:
    def __init__(self):
        self.stages = {}

    def run(self, cmd, *rest, **kw):
        cmd = [str(c) for c in cmd]
        if cmd[1] == "to-legacy":
            names = {cmd[i + 1] for i, c in enumerate(cmd) if c == "-f"}
            content = Path(cmd[-1]) / "OblivionRemastered/Content"
            for asset in TEXTURES:
                if Path(asset).name in names:
                    p = content / asset
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.with_suffix(".uasset").write_bytes(b"uasset")
                    p.with_suffix(".uexp").write_bytes(layout_bytes(Path(asset).name))
            for package, row in LAYOUT.items():
                if package in names:
                    p = content / row["asset"]
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.with_suffix(".uasset").write_bytes(b"uasset")
                    p.with_suffix(".uexp").write_bytes(layout_bytes(package))
            for masks in pipe.GLYPH_MASKS.values():
                for name in masks:
                    p = content / f"Art/UI/FakeMasks/{name}"
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.with_suffix(".uasset").write_bytes(b"uasset")
                    p.with_suffix(".uexp").write_bytes(b"uexp")
            return ""
        if cmd[1] == "to-zen":
            stage, utoc = Path(cmd[-2]), Path(cmd[-1])
            self.stages[utoc.stem] = {p.relative_to(stage).as_posix().lower(): p.with_suffix(".uexp").read_bytes()
                                      for p in stage.rglob("*.uasset")}
            for ext in ("pak", "utoc", "ucas"):
                utoc.with_suffix("." + ext).write_bytes(b"pack")
            return ""
        raise AssertionError(cmd)


@contextlib.contextmanager
def faked(fake):
    saved = {k: getattr(pipe, k) for k in ("run", "dds_tools", "recipe", "LAUREL_STOCK_SHA256", "ALCHEMY_STOCK_SHA256")}
    pipe.run, pipe.recipe = fake.run, fake_recipe
    pipe.LAUREL_STOCK_SHA256 = hashlib.sha256(layout_bytes("-")).hexdigest()
    pipe.ALCHEMY_STOCK_SHA256 = {Path(a).name: hashlib.sha256(layout_bytes("-")).hexdigest() for a, r in TEXTURES.items() if r == "alchemy"}

    def export(ua, out):
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        Path(out).write_bytes(dds())
    pipe.dds_tools = lambda path=None: (export, lambda ua, d, out: None)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            yield
    finally:
        for k, v in saved.items():
            setattr(pipe, k, v)


with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    retoc = temp / "retoc.exe"
    retoc.write_bytes(b"")
    args = SimpleNamespace(retoc=retoc, paks=temp / "Paks", ddstools=None)
    if callable(getattr(pipe, "build_layout", None)):
        fake = Fake()
        sizes = dict(cc.SIZES) | {"inventoryRowHeight": 40.0}
        with faked(fake):
            lay = pipe.build_layout(args, temp / "outL", sizes, log=None)
        staged = fake.stages.get("MRLayout_P", {})
        check(len(staged) == 20 and lay.get("pack") == "MRLayout_P",
              f"MRLayout_P stages the 20 layout packages that pass their checks ({len(staged)})")
        check(lay.get("sizes", {}).get("inventoryRowHeight") == 40.0, "the layout manifest records the sizes it baked")
        inv = [b for k, b in staged.items() if k.endswith("wbp_originalmenu_inventoryentry.uasset")]
        off = [f["offset"] for f in LAYOUT["WBP_OriginalMenu_InventoryEntry"]["floats"] if f.get("size") == "inventoryRowHeight"][0]
        check(inv and struct.unpack_from("<f", inv[0], off)[0] == 40.0, "MRLayout_P bakes the row height")
        skipped = {Path(e["asset"]).name for e in lay.get("skipped", [])}
        check(skipped == {"WBP_ModernMenu_AlchemyFilters", "WBP_ModernMenu_AlchemyItemsListView"},
              f"a rewrite whose stock export differs is skipped and listed, the rest is built ({sorted(skipped)})")
        check(not any("t_" in k.split("/")[-1][:2] for k in staged), "MRLayout_P holds no colour asset")
        check(lay.get("game_build") == pipe.GAME_BUILD, "the layout manifest names the game build (the plugin checks it)")

        fake = Fake()
        with faked(fake):
            man = pipe.build(args, temp / "outC", "MRCustom_P", None, sizes=sizes, log=None)
        staged = fake.stages.get("MRCustom_P", {})
        names = [k.split("/")[-1] for k in staged]
        check(len(names) == len(set(names)), "a package in both data sets is staged once")
        check(sum(1 for n in names if n[:-len(".uasset")] in {p.lower() for p in LAYOUT}) >= 20,
              "MRCustom_P also holds the layout packages (decision 46)")
        check(man.get("sizes", {}).get("inventoryRowHeight") == 40.0, "the colour pack's manifest records its sizes")
        inv = [b for k, b in staged.items() if k.endswith("wbp_originalmenu_inventoryentry.uasset")]
        check(inv and struct.unpack_from("<f", inv[0], off)[0] == 40.0, "MRCustom_P bakes the row height too")

        fake = Fake()
        with faked(fake):
            plain = pipe.build(args, temp / "outP", "MRCustom_P", None, log=None)
        check("sizes" not in plain or not plain["sizes"], "without sizes MRCustom_P holds colours only (bLayoutPack=0)")

    # ---- E. app.build_pack writes both packs; bLayoutPack=0 writes only the colour pack ----
    from pack_builder import app
    check(callable(getattr(app, "layout_enabled", None)), "app.layout_enabled reads [Layout] bLayoutPack")
    if callable(getattr(app, "layout_enabled", None)):
        plugin = temp / "Plugins/MenusRevamped"
        plugin.mkdir(parents=True)
        colours = plugin / "CustomColours.ini"
        check(app.layout_enabled(colours) is True, "no MenusRevamped.ini = layout pack on (default 1)")
        (plugin.parent / "MenusRevamped.ini").write_text("[Layout]\nbLayoutPack=0\n", encoding="utf-8")
        check(app.layout_enabled(colours) is False, "[Layout] bLayoutPack=0 turns the layout pack off")

# ---- F. --check covers the sizes pack and the sizes ----
cli = (root / "tools/make_dark_pack.py").read_text(encoding="utf-8")
from pack_builder import app as app_module  # noqa: E402
check('"sizes"' in cli.split("COMPARED", 1)[1].splitlines()[0], "--check compares the sizes a pack was baked with")
check("layout_manifest" in cli and "LAYOUT_PACK" in cli, "--check also compares MRLayout_P with --out")
check("layout_manifest" in getattr(app_module.Result, "__dataclass_fields__", {}), "the build result carries the layout manifest")

print(f"{count} checks, {len(fails)} failures")
for why in fails:
    print("FAIL:", why)
sys.exit(1 if fails else 0)
